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
                    return '_eglGetProcAddress\n_EGL_GetProcAddress\n'
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

    def test_missing_direct_egl_implementation_export_fails_build(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            (prefix / 'lib').mkdir()
            for name in ('libEGL.dylib', 'libGLESv2.dylib'):
                (prefix / 'lib' / name).touch()
            def output(args, **kwargs):
                return '_eglGetProcAddress\n' if args[0] == 'xcrun' else args[-1] + ':\n'
            with patch.object(angle.subprocess, 'check_output', side_effect=output), patch.object(angle.subprocess, 'run'):
                with self.assertRaisesRegex(ValueError, 'GLES library does not export EGL_GetProcAddress'):
                    angle.normalize(prefix)

    def test_restricted_webkit_binary_is_rejected_before_normalizing(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            (prefix / 'lib').mkdir()
            for name in ('libEGL.dylib', 'libGLESv2.dylib'):
                (prefix / 'lib' / name).touch()
            with patch.object(angle.subprocess, 'check_output', return_value='cmd LC_SUB_CLIENT\nclient WebCore'), patch.object(angle.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'retains WebKit client restrictions'):
                    angle.normalize(prefix)
                run.assert_not_called()


class AngleSDKCompatibilityTests(unittest.TestCase):
    def test_patch_is_idempotent_and_rejects_source_drift(self):
        spec = importlib.util.spec_from_file_location('prepare_angle', ROOT / 'scripts/prepare_angle.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dynamic = root / 'Source/ThirdParty/ANGLE/Configurations/ANGLE-dynamic.xcconfig'
            dynamic.parent.mkdir(parents=True)
            dynamic.write_text('ANGLE_ALLOWABLE_CLIENTS_YES = -allowable_client WebCore -allowable_client WebCoreTestSupport;')
            config = root / 'Configurations/CommonBase.xcconfig'
            header = root / 'Source/ThirdParty/ANGLE/src/common/bitset_utils.h'
            config.parent.mkdir(parents=True)
            header.parent.mkdir(parents=True)
            config.write_text('\n'.join(['-D_LIBCPP_ENABLE_ASSERTIONS=1'] * 5))
            header.write_text('if (priv::kDefaultBitSetSize < 64)')
            module.prepare(root)
            self.assertNotIn('-allowable_client', dynamic.read_text())
            expected = (config.read_text(), header.read_text())
            module.prepare(root)
            self.assertEqual(expected, (config.read_text(), header.read_text()))
            self.assertNotIn('_LIBCPP_ENABLE_ASSERTIONS', expected[0])
            header.write_text('changed upstream')
            with self.assertRaises(ValueError):
                module.prepare(root)


class AngleCMakeDiscoveryTests(unittest.TestCase):
    def test_explicit_angle_survives_sdk_rooted_search_and_missing_file_fails(self):
        import subprocess
        with tempfile.TemporaryDirectory(prefix='angle discovery ') as directory:
            root = Path(directory)
            library = root / 'libGLESv2.dylib'
            library.touch()
            # Exercise the real Apple CMake branch on Linux, without pretending
            # to compile or link iOS code. ONLY reproduces SDK-rooted discovery.
            command = [
                'cmake', '-S', str(ROOT / 'ThirdParty/EmuGL'), '-B', str(root / 'build'),
                '-DAPPLE=TRUE', '-DEMUGEN=/bin/true',
                f'-DANGLE_GLES_LIBRARY:FILEPATH={library}',
                f'-DCMAKE_FIND_ROOT_PATH={root / "sdk"}',
                '-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY',
            ]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            library.unlink()
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('ANGLE_GLES_LIBRARY must name an existing', result.stderr)


class LazyInstanceTests(unittest.TestCase):
    def test_concurrent_construction_and_publication(self):
        import subprocess
        import os
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'lazy-test')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2', '-pthread',
                            '-I' + str(ROOT / 'ThirdParty/EmuGL/upstream/shared'),
                            str(ROOT / 'Tests/LazyInstanceTests.cpp'),
                            str(ROOT / 'ThirdParty/EmuGL/upstream/shared/emugl/common/lazy_instance.cpp'),
                            '-o', executable], check=True, capture_output=True)
            subprocess.run([executable], check=True, timeout=30)


class DependencyCacheTests(unittest.TestCase):
    def test_toolchain_and_build_input_changes_invalidate_cache(self):
        spec = importlib.util.spec_from_file_location('dependency_key', ROOT / 'scripts/ios_dependency_key.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        key = module.fingerprint(ROOT, 'Xcode A')
        self.assertEqual(key, module.fingerprint(ROOT, 'Xcode A'))
        self.assertNotEqual(key, module.fingerprint(ROOT, 'Xcode B'))
        original = Path.read_bytes
        def changed(path):
            data = original(path)
            return data + b'new patch' if path.name == 'prepare_angle.py' else data
        with patch.object(Path, 'read_bytes', changed):
            self.assertNotEqual(key, module.fingerprint(ROOT, 'Xcode A'))
