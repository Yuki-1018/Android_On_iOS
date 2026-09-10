#!/usr/bin/env python3
"""Build an unsigned iOS framework dependency closure; reject host dylibs."""
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys


def run(*args):
    return subprocess.check_output(args, text=True)

def package(engine, prefix, destination):
    engine, prefix, destination = (Path(p).resolve() for p in (engine, prefix, destination))
    closure, imports = {}, {}
    def visit(path, main=False):
        path = path.resolve(strict=True)
        if path in closure:
            return
        if not main and not path.is_relative_to(prefix):
            raise ValueError(f'Non-iOS dependency outside sysroot: {path}')
        name = 'AndroidQEMU' if main else re.sub(r'[^A-Za-z0-9_]', '_', path.name.removesuffix('.dylib'))
        if name in closure.values():
            raise ValueError('Framework name collision')
        if run('xcrun', 'lipo', '-archs', str(path)).strip() != 'arm64':
            raise ValueError(f'Expected thin arm64: {path}')
        commands = run('xcrun', 'vtool', '-show-build', str(path))
        if not re.search(r'platform\s+(IOS|2)\b', commands):
            raise ValueError(f'Expected iPhoneOS Mach-O: {path}')
        closure[path] = name
        deps = []
        for line in run('otool', '-L', str(path)).splitlines()[2:]:
            dep = line.strip().split(' (compatibility version', 1)[0]
            if dep.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dep.startswith('@rpath/') or dep.startswith('@loader_path/'):
                target = prefix / 'lib' / Path(dep).name
            else:
                target = Path(dep)
            target = target.resolve(strict=True)
            deps.append((dep, target))
            visit(target)
        imports[path] = deps
    visit(engine, True)
    destination.mkdir(parents=True, exist_ok=True)
    # Only replace framework names owned by this generated closure.
    for path, name in closure.items():
        folder = destination / (name + '.framework')
        if folder.exists(): shutil.rmtree(folder)
        folder.mkdir()
        binary = folder / name
        shutil.copy2(path, binary)
        subprocess.run(['codesign', '--remove-signature', str(binary)], capture_output=True)
        if subprocess.run(['codesign', '--verify', str(binary)], capture_output=True).returncode == 0:
            raise ValueError('Framework signature was not removed')
        subprocess.run(['install_name_tool', '-id', f'@rpath/{name}.framework/{name}', str(binary)], check=True)
        for original, target in imports[path]:
            other = closure[target]
            subprocess.run(['install_name_tool', '-change', original, f'@rpath/{other}.framework/{other}', str(binary)], check=True)
        (folder / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': name, 'CFBundleIdentifier': 'org.androidemu.engine.' + name.replace('_', '-'),
            'CFBundlePackageType': 'FMWK', 'CFBundleShortVersionString': '1.0', 'CFBundleVersion': '1',
            'MinimumOSVersion': '26.0', 'CFBundleSupportedPlatforms': ['iPhoneOS']}))
    (destination / 'engine-manifest.json').write_text(json.dumps({'frameworks': sorted(closure.values())}, indent=2) + '\n')

if __name__ == '__main__':
    package(*sys.argv[1:])
