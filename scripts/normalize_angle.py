#!/usr/bin/env python3
"""Normalize the pinned ANGLE archive's install names inside the iOS sysroot.

Xcode archives use /usr/local/lib install names. Preserve real link dependencies
for framework packaging. EmuGL links the GLES implementation directly; verify
its EGL entry point so the WebKit filename-based loader is never required.
"""
from pathlib import Path
import subprocess
import sys


def normalize(prefix):
    lib = Path(prefix).resolve() / 'lib'
    owned = {name: (lib / name).resolve(strict=True) for name in ('libEGL.dylib', 'libGLESv2.dylib')}
    # An install-name rewrite does not remove WebKit's LC_SUB_CLIENT allowlist.
    # Reject stale/restricted builds before the linker encounters them.
    for path in owned.values():
        commands = subprocess.check_output(['otool', '-l', str(path)], text=True)
        if 'LC_SUB_CLIENT' in commands:
            raise ValueError(f'ANGLE library retains WebKit client restrictions; rebuild dependencies: {path}')
    for path in owned.values():
        lines = subprocess.check_output(['otool', '-L', str(path)], text=True).splitlines()[2:]
        subprocess.run(['install_name_tool', '-id', str(path), str(path)], check=True)
        for line in lines:
            dependency = line.strip().split(' (compatibility version', 1)[0]
            target = owned.get(Path(dependency).name)
            if target:
                subprocess.run(['install_name_tool', '-change', dependency, str(target), str(path)], check=True)
    symbols = subprocess.check_output(['xcrun', 'nm', '-gUj', str(owned['libEGL.dylib'])], text=True).splitlines()
    if '_eglGetProcAddress' not in symbols:
        raise ValueError('ANGLE EGL library does not export eglGetProcAddress')
    symbols = subprocess.check_output(['xcrun', 'nm', '-gUj', str(owned['libGLESv2.dylib'])], text=True).splitlines()
    if '_EGL_GetProcAddress' not in symbols:
        raise ValueError('ANGLE GLES library does not export EGL_GetProcAddress')


if __name__ == '__main__':
    normalize(sys.argv[1])
