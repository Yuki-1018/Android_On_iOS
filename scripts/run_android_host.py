#!/usr/bin/env python3
"""Boot a user-owned, raw Android 4–6 ARMv7 Goldfish ext4 bundle with the development host engine.

No image download, conversion, formatting or network listener is performed.
Guest serial output goes to this terminal; display can be inspected via local QMP.
"""
import argparse
import os
from pathlib import Path
import stat
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def qemu_option_path(path):
    value = str(path)
    if '\0' in value or '\n' in value or '\r' in value:
        raise ValueError('Invalid path')
    return value.replace(',', ',,')

def arguments(folder, ram=640, cache=192, qmp=None):
    folder = Path(folder).resolve(strict=True)
    if ram not in (512, 640, 768, 1024) or cache not in (128, 192, 256):
        raise ValueError('Unsupported fixed profile setting')
    properties = folder / 'source.properties'
    if properties.is_symlink() or not properties.is_file() or properties.stat().st_size > 65536:
        raise ValueError('A regular source.properties file is required')
    values = {}
    for line in properties.read_text().splitlines():
        if line.lstrip().startswith(('#', '!')) or '=' not in line:
            continue
        key, value = (part.strip() for part in line.split('=', 1))
        if key in values and values[key] != value:
            raise ValueError('Conflicting SDK metadata')
        values[key] = value
    versions = {14: '4.0', 15: '4.0', 16: '4.1', 17: '4.2', 18: '4.3', 19: '4.4', 21: '5.0', 22: '5.1', 23: '6.0'}
    try:
        api = int(values.get('AndroidVersion.ApiLevel', ''))
    except ValueError:
        raise ValueError('Missing Android API level')
    if api not in versions or values.get('SystemImage.Abi') != 'armeabi-v7a':
        raise ValueError('Expected Android 4–6 ARMv7 Goldfish metadata')
    tag = values.get('SystemImage.TagId')
    if tag != 'default' and not (tag is None and api <= 18):
        raise ValueError('Expected default system image')
    version = values.get('Platform.Version', versions[api])
    if version != versions[api] and not version.startswith(versions[api] + '.'):
        raise ValueError('Conflicting Android API/version')
    for key, expected in [('hw.cpu.arch', 'arm'), ('hw.board', 'goldfish')]:
        if values.get(key, expected) != expected:
            raise ValueError('Expected ARM Goldfish board')
    kernel = folder / ('kernel-qemu' if (folder / 'kernel-qemu').exists() else 'kernel')
    files = [kernel, folder / 'ramdisk.img', folder / 'system.img', folder / 'userdata.img']
    if os.path.lexists(folder / 'cache.img'):
        files.append(folder / 'cache.img')
    for index, path in enumerate(files):
        info = path.lstat()
        maximum = 64 * 1024**2 if index < 2 else 8 * 1024**3
        if not stat.S_ISREG(info.st_mode) or info.st_size <= 0 or info.st_size > maximum:
            raise ValueError(f'Invalid image file: {path.name}')
        if index >= 2 and info.st_size % 4096:
            raise ValueError(f'{path.name} must be aligned to 4096 bytes')
        with path.open('rb') as stream:
            if stream.read(4) == struct.pack('<I', 0xed26ff3a):
                raise ValueError(f'{path.name} is sparse; import/convert to raw before host boot')
    with kernel.open('rb') as stream:
        stream.seek(36)
        if stream.read(4) != b'\x18\x28\x6f\x01':
            raise ValueError('Expected an ARM zImage kernel')
    args = ['-machine', 'android51,audiodev=audio', '-cpu', 'cortex-a8', '-m', str(ram), '-smp', '1',
            '-accel', f'tcg,tb-size={cache},split-wx=on', '-nodefaults', '-no-reboot',
            '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
            '-audiodev', 'none,id=audio', '-nic', 'user,model=smc91c111,ipv6=off',
            '-kernel', str(kernel), '-initrd', str(folder / 'ramdisk.img'),
            '-append', 'qemu=1 console=ttyS0 androidboot.console=ttyS0 androidboot.hardware=goldfish qemu.gles=0 android.qemud=1']
    for path in files[2:]:
        name = path.stem
        args.extend(['-drive', f'if=none,id={name},format=raw,file={qemu_option_path(path)},readonly={"on" if name == "system" else "off"}'])
    if qmp is not None:
        path = Path(qmp).absolute()
        if os.path.lexists(path):
            raise ValueError('Refusing to overwrite an existing QMP socket')
        args.extend(['-qmp', f'unix:{qemu_option_path(path)},server=on,wait=off'])
    return args

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', type=Path)
    parser.add_argument('--qemu', type=Path, default=ROOT / 'build/qemu-host/qemu-system-arm')
    parser.add_argument('--ram', type=int, default=640)
    parser.add_argument('--cache', type=int, default=192)
    parser.add_argument('--qmp', type=Path, help='Optional local Unix socket for diagnostics; never a TCP listener')
    options = parser.parse_args()
    argv = arguments(options.images, options.ram, options.cache, options.qmp)
    return subprocess.call([str(options.qemu.resolve(strict=True)), *argv])

if __name__ == '__main__':
    raise SystemExit(main())
