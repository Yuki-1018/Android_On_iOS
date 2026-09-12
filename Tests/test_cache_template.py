import importlib.util
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cache', ROOT / 'scripts/create_cache_template.py')
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


class CacheTemplateTests(unittest.TestCase):
    def test_sparse_holes_preserve_running_and_final_crc(self):
        importer = Path(os.environ.get('ANDROID51_IMAGE_COPY', ROOT / 'build/native/image_copy'))
        if not importer.is_file():
            self.skipTest('Build the native image_copy target first')
        def chunk(kind, blocks, data=b''):
            return struct.pack('<2H2I', kind, 0, blocks, 12 + len(data)) + data
        # A hole after nonzero bytes catches incorrect CRC initial-state handling.
        prefix = b'ABCD'
        crc = zlib.crc32(prefix)
        crc = zlib.crc32(bytes(65532), crc)
        intermediate = crc
        fill = b'WXYZ' * 1024
        crc = zlib.crc32(fill, crc)
        remaining = cache.SIZE - 65536 - len(fill)
        zeros = bytes(65536)
        for offset in range(0, remaining, len(zeros)):
            crc = zlib.crc32(zeros[:min(len(zeros), remaining - offset)], crc)
        chunks = (chunk(0xcac1, 1, prefix) + chunk(0xcac3, 16383) +
                  chunk(0xcac4, 0, struct.pack('<I', intermediate)) +
                  chunk(0xcac2, 1024, b'WXYZ') + chunk(0xcac3, remaining // 4) +
                  chunk(0xcac4, 0, struct.pack('<I', crc)))
        header = struct.pack('<I4H4I', 0xed26ff3a, 1, 0, 28, 12, 4, cache.SIZE // 4, 6, crc)
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / 'input.sparse', Path(folder) / 'output.img'
            source.write_bytes(header + chunks)
            subprocess.run([str(importer), str(source), str(output)], check=True)
            self.assertEqual(output.stat().st_size, cache.SIZE)
            with output.open('rb') as f:
                self.assertEqual(f.read(4), prefix)
                f.seek(65536)
                self.assertEqual(f.read(len(fill)), fill)
                f.seek(cache.SIZE - 16)
                self.assertEqual(f.read(), bytes(16))
            output.unlink()
            source.write_bytes(header + chunks[:-1] + bytes([chunks[-1] ^ 1]))
            result = subprocess.run([str(importer), str(source), str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_native_import_produces_clean_legacy_ext4_without_overwrite(self):
        importer = Path(os.environ.get('ANDROID51_IMAGE_COPY', ROOT / 'build/native/image_copy'))
        if not importer.is_file():
            self.skipTest('Build the native image_copy target first')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            template = cache.create(root / 'cache.sparse')
            image = root / 'cache.img'
            subprocess.run([str(importer), str(template), str(image)], check=True)
            self.assertEqual(image.stat().st_size, cache.SIZE)
            subprocess.run([cache.tool('e2fsck'), '-fn', str(image)], check=True, capture_output=True)
            with image.open('rb') as file:
                file.seek(1024)
                sb = file.read(1024)
            self.assertEqual(sb[56:58], b'\x53\xef')
            self.assertEqual(sb[120:125], b'cache')
            compat, incompat, ro_compat = struct.unpack_from('<3I', sb, 92)
            self.assertEqual(compat, 0x0c)  # has_journal, ext_attr
            self.assertEqual(incompat, 0x42)  # filetype, extents
            self.assertEqual(ro_compat, 0x03)  # sparse_super, large_file
            before = image.read_bytes()
            result = subprocess.run([str(importer), str(template), str(image)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(image.read_bytes(), before)

    def test_same_source_imports_have_independent_writable_disks(self):
        importer = Path(os.environ.get('ANDROID51_IMAGE_COPY', ROOT / 'build/native/image_copy'))
        if not importer.is_file():
            self.skipTest('Build the native image_copy target first')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = cache.create(root / 'source.sparse')
            original = source.read_bytes()
            for disk in ('userdata.img', 'cache.img'):
                outputs = [root / profile / disk for profile in ('profile-a', 'profile-b')]
                for output in outputs:
                    output.parent.mkdir(exist_ok=True)
                    subprocess.run([str(importer), str(source), str(output)], check=True)
                self.assertNotEqual(outputs[0].stat().st_ino, outputs[1].stat().st_ino)
                with outputs[1].open('rb') as other:
                    other.seek(8192)
                    before = other.read(32)
                with outputs[0].open('r+b') as changed:
                    changed.seek(8192)
                    changed.write(bytes(b ^ 0xff for b in before))
                with outputs[1].open('rb') as other:
                    other.seek(8192)
                    self.assertEqual(other.read(32), before)
                self.assertEqual(source.read_bytes(), original)
