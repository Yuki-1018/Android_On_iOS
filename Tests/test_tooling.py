import importlib.util
import plistlib
import json
import stat
import struct
import tempfile
import subprocess
import unittest
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify', ROOT / 'scripts/verify_artifact.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

class ArtifactTests(unittest.TestCase):
    def make_ipa(self, path, extra=None):
        info = {'MinimumOSVersion': '26.0', 'CFBundleExecutable': 'AndroidEmu', 'LSApplicationQueriesSchemes': ['stikdebug']}
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('Payload/AndroidEmu.app/Info.plist', plistlib.dumps(info))
            archive.writestr('Payload/AndroidEmu.app/AndroidEmu', struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, 0, 0, 0, 0))
            archive.writestr('Payload/AndroidEmu.app/EngineLicenses/engine-manifest.json', json.dumps({'frameworks': ['AndroidQEMU']}))
            archive.writestr('Payload/AndroidEmu.app/Frameworks/AndroidQEMU.framework/AndroidQEMU', struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 6, 0, 0, 0, 0))
            if extra:
                archive.writestr(extra, b'fixture')

    def test_structure(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'app.ipa'
            self.make_ipa(path)
            verify.verify(path)

    def test_guest_and_path_rejection(self):
        for bad in ['Payload/AndroidEmu.app/system.img', 'Payload/AndroidEmu.app/x.apk', '../Payload/evil',
                    'Payload/../outside', '/Payload/evil', 'Payload/AndroidEmu.app/kernel',
                    'Payload/AndroidEmu.app/_CodeSignature/CodeResources']:
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / 'app.ipa'
                self.make_ipa(path, bad)
                with self.assertRaises(ValueError):
                    verify.verify(path)

    def test_symlink(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'app.ipa'
            link = zipfile.ZipInfo('Payload/AndroidEmu.app/link')
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            self.make_ipa(path, link)
            with self.assertRaises(ValueError):
                verify.verify(path)

class ProjectTests(unittest.TestCase):
    def test_project_is_reproducible(self):
        project = ROOT / 'AndroidEmu.xcodeproj/project.pbxproj'
        before = project.read_bytes()
        subprocess.run(['python3', str(ROOT / 'scripts/generate_project.py')], check=True)
        self.assertEqual(before, project.read_bytes())

    def test_plists_and_scheme(self):
        for name in ['App/Info.plist', 'AndroidEmu.entitlements']:
            with (ROOT / name).open('rb') as source:
                self.assertIsInstance(plistlib.load(source), dict)
        ET.parse(ROOT / 'AndroidEmu.xcodeproj/xcshareddata/xcschemes/AndroidEmu.xcscheme')

if __name__ == '__main__':
    unittest.main()
