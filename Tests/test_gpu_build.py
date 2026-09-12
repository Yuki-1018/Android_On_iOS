import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('normalize_angle', ROOT / 'scripts/normalize_angle.py')
angle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(angle)


class AnglePackagingTests(unittest.TestCase):
    def test_xcode_archive_install_names_are_mapped_into_sysroot(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            (prefix / 'lib').mkdir()
            for name in ('libEGL.dylib', 'libGLESv2.dylib'):
                (prefix / 'lib' / name).touch()
            def output(args, **kwargs):
                if args[0] == 'xcrun':
                    return '_eglGetProcAddress\n'
                return (args[-1] + ':\n /usr/local/lib/' + Path(args[-1]).name + ' (compatibility version 1.0.0)\n'
                        ' /usr/local/lib/libGLESv2.dylib (compatibility version 1.0.0)\n'
                        ' /System/Library/Frameworks/Metal.framework/Metal (compatibility version 1.0.0)\n')
            with patch.object(angle.subprocess, 'check_output', side_effect=output), patch.object(angle.subprocess, 'run') as run:
                angle.normalize(prefix)
            changes = [call.args[0] for call in run.call_args_list if '-change' in call.args[0]]
            self.assertEqual(len(changes), 2)
            for args in changes:
                self.assertEqual(args[2], '/usr/local/lib/libGLESv2.dylib')
                self.assertEqual(args[3], str(prefix / 'lib/libGLESv2.dylib'))

    def test_missing_angle_library_is_not_silently_omitted(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                angle.normalize(Path(directory))
