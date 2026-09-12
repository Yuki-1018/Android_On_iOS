#!/usr/bin/env python3
"""Normalize the pinned ANGLE archive's install names inside the iOS sysroot.

Xcode archives use /usr/local/lib install names. Preserve real link dependencies
so package_engine_frameworks can discover and embed both EGL and GLES dylibs.
"""
from pathlib import Path
import subprocess
import sys


def normalize(prefix):
    lib = Path(prefix).resolve() / 'lib'
    owned = {name: (lib / name).resolve(strict=True) for name in ('libEGL.dylib', 'libGLESv2.dylib')}
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


if __name__ == '__main__':
    normalize(sys.argv[1])
