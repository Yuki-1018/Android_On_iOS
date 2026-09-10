"""Reproduce same-architecture cross compiler sanity checks without an iOS SDK."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MESON = os.environ.get('MESON', shutil.which('meson'))


@unittest.skipUnless(MESON and shutil.which('cc'), 'requires Meson and a C compiler')
class CrossBuildTests(unittest.TestCase):
    def test_device_executables_are_never_run_on_build_host(self):
        version = subprocess.check_output([MESON, '--version'], text=True).strip()
        if tuple(map(int, version.split('.')[:2])) < (1, 5):
            self.skipTest('regression requires QEMU minimum Meson 1.5')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            source.mkdir()
            (source / 'meson.build').write_text("project('device-sanity', 'c')\n")
            compiler = root / 'device-cc'
            # Compile normally but emulate an executable rejected by the host OS.
            compiler.write_text(
                f'#!{sys.executable}\n'
                'import pathlib, subprocess, sys\n'
                f'result = subprocess.run([{shutil.which("cc")!r}, *sys.argv[1:]])\n'
                'if result.returncode == 0 and "-o" in sys.argv:\n'
                '    output = pathlib.Path(sys.argv[sys.argv.index("-o") + 1])\n'
                '    if "sanity" in output.name:\n'
                '        output.write_text("#!/bin/sh\\nexit 126\\n")\n'
                'sys.exit(result.returncode)\n')
            compiler.chmod(0o755)
            cpu = {'arm64': 'aarch64', 'AMD64': 'x86_64'}.get(platform.machine(), platform.machine())
            system = {'Darwin': 'darwin', 'Linux': 'linux'}.get(platform.system(), platform.system().lower())
            for needs_wrapper in (False, True):
                cross = root / f'{needs_wrapper}.cross'
                cross.write_text(
                    f'[binaries]\nc = {str(compiler)!r}\n'
                    f'[properties]\nneeds_exe_wrapper = {str(needs_wrapper).lower()}\n'
                    f'[host_machine]\nsystem = {system!r}\ncpu_family = {cpu!r}\n'
                    f'cpu = {cpu!r}\nendian = {sys.byteorder!r}\n')
                env = os.environ.copy()
                for key in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'SDKROOT'):
                    env.pop(key, None)
                result = subprocess.run(
                    [MESON, 'setup', str(root / f'build-{needs_wrapper}'), str(source), '--cross-file', str(cross)],
                    env=env, capture_output=True, text=True)
                if needs_wrapper:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('not runnable', result.stdout + result.stderr)

    def test_pinned_qemu_patch_sets_wrapper_requirement(self):
        patch = (ROOT / 'ThirdParty/AndroidQemuCompat/patches/0004-cross-build-no-host-execution.patch').read_text()
        self.assertIn('+  if test "$cross_compile" = "yes"; then', patch)
        self.assertIn('+    echo "needs_exe_wrapper = true" >> $cross', patch)
