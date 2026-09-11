import importlib.util
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cache', ROOT / 'scripts/create_cache_template.py')
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


class CacheTemplateTests(unittest.TestCase):
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
