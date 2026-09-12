import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'build/native/zip_extract'

class ZipTests(unittest.TestCase):
    def run_archive(self, create, valid):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / 'image.zip', Path(tmp) / 'expanded'
            with zipfile.ZipFile(source, 'w') as archive:
                create(archive)
            result = subprocess.run([str(TOOL), str(source), str(output)], capture_output=True)
            self.assertEqual(result.returncode == 0, valid, result.stderr.decode())
            if not valid:
                self.assertFalse(output.exists())
            return output.exists()

    def test_stored_deflated_nested_empty_and_large(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / 'image.zip', Path(tmp) / 'expanded'
            data = bytes(range(256)) * 8192
            with zipfile.ZipFile(source, 'w') as archive:
                archive.writestr('android/', b'')
                archive.writestr('android/source.properties', b'AndroidVersion.ApiLevel=23')
                archive.writestr('android/system.img', data, compress_type=zipfile.ZIP_DEFLATED)
                archive.writestr('empty', b'', compress_type=zipfile.ZIP_DEFLATED)
            subprocess.run([str(TOOL), str(source), str(output)], check=True)
            self.assertEqual((output / 'android/system.img').read_bytes(), data)
            self.assertEqual((output / 'empty').read_bytes(), b'')
            result = subprocess.run([str(TOOL), str(source), str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((output / 'android/system.img').read_bytes(), data)

    def test_rejects_traversal_absolute_and_links(self):
        for name in ['../outside', '/outside', 'a/../../outside', 'a\\..\\outside', 'C:/outside']:
            with self.subTest(name=name):
                self.run_archive(lambda z: z.writestr(name, b'bad'), False)
        def symlink(z):
            info = zipfile.ZipInfo('link'); info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, b'../outside')
        self.run_archive(symlink, False)

    def test_checksum_failure_rolls_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / 'image.zip', Path(tmp) / 'expanded'
            with zipfile.ZipFile(source, 'w') as z: z.writestr('system.img', b'PAYLOAD')
            data = source.read_bytes().replace(b'PAYLOAD', b'DAMAGED')
            source.write_bytes(data)
            result = subprocess.run([str(TOOL), str(source), str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_cancellation_rolls_back_partial_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / 'image.zip', Path(tmp) / 'expanded'
            with zipfile.ZipFile(source, 'w', compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('system.img', bytes(2 << 20))
            result = subprocess.run([str(TOOL), str(source), str(output), 'cancel'], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'cancelled', result.stderr)
            self.assertFalse(output.exists())

    def test_rejects_file_directory_collision(self):
        def archive(z):
            z.writestr('android', b'file')
            z.writestr('android/source.properties', b'bad')
        self.run_archive(archive, False)
