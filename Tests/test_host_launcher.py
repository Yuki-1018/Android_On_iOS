import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('host', ROOT / 'scripts/run_android_host.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class HostLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ae,host-')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        (self.folder / 'source.properties').write_text(
            'AndroidVersion.ApiLevel=22\nSystemImage.Abi=armeabi-v7a\nSystemImage.TagId=default\n')
        (self.folder / 'kernel-qemu').write_bytes(bytes(36) + b'\x18\x28\x6f\x01')
        for name in ['ramdisk.img', 'system.img', 'userdata.img']:
            (self.folder / name).write_bytes(bytes(4096))

    def test_fixed_profile_and_readonly_system(self):
        argv = host.arguments(self.folder)
        self.assertEqual(argv[argv.index('-smp') + 1], '1')
        drives = [argv[i + 1] for i, v in enumerate(argv) if v == '-drive']
        self.assertIn('readonly=on', drives[0])
        self.assertIn('readonly=off', drives[1])
        self.assertIn('ae,,host-', drives[0])
        self.assertFalse(any('hostfwd' in arg for arg in argv))

    def test_rejects_sparse_and_unaligned_images(self):
        image = self.folder / 'system.img'
        for data in [b'\x3a\xff\x26\xed' + bytes(4092), bytes(4095)]:
            image.write_bytes(data)
            with self.assertRaises(ValueError):
                host.arguments(self.folder)

    def test_rejects_symlink_images_and_qmp_paths(self):
        image = self.folder / 'system.img'
        image.unlink()
        image.symlink_to(self.folder / 'userdata.img')
        with self.assertRaises(ValueError):
            host.arguments(self.folder)
        image.unlink()
        image.write_bytes(bytes(4096))
        qmp = self.folder / 'qmp.sock'
        qmp.symlink_to(self.folder / 'missing')
        with self.assertRaises(ValueError):
            host.arguments(self.folder, qmp=qmp)

    def test_rejects_mismatched_or_conflicting_metadata(self):
        properties = self.folder / 'source.properties'
        original = properties.read_text()
        for suffix in ['Platform.Version=5.0.2\n', 'AndroidVersion.ApiLevel=23\n']:
            properties.write_text(original + suffix)
            with self.assertRaises(ValueError):
                host.arguments(self.folder)


if __name__ == '__main__':
    unittest.main()
