#!/usr/bin/env python3
"""Build the optional ARMv7 HIGHMEM kernel on Linux from pinned AOSP inputs.

The default guest kernel is retained at <=760 MiB. No Android system/userdata
image is downloaded or modified. Ship the source archive and config with IPA.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_REV = '880d9af358076df842377facd4b900f6bbecc783'
GCC_REV = '26e93f6af47f7bd3a9beb5c102a5f45e19bfa38a'
INPUTS = (
    ('source', f'https://android.googlesource.com/kernel/goldfish/+archive/{SOURCE_REV}.tar.gz',
     'b4f02ad477b063f83d14e5070a08d2772633b7360453bfdf9f4dea7d5cb980c9'),
    ('toolchain', f'https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/+archive/{GCC_REV}.tar.gz',
     'a68bc3321919863b0dc21f2242b505f9b40e766456bc6633125aae2cc22a2722'),
)

def verified_archive(path, url, digest):
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        partial = path.with_suffix('.partial')
        subprocess.run(['curl', '-fL', '--retry', '4', '--connect-timeout', '30', '-o', str(partial), url], check=True)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != digest:
            partial.unlink()
            raise RuntimeError(f'Checksum mismatch: {url}')
        partial.replace(path)

def main():
    work = ROOT / 'build/highmem'
    work.mkdir(parents=True, exist_ok=True)
    for name, url, digest in INPUTS:
        archive = work / f'{name}.tar.gz'
        verified_archive(archive, url, digest)
        destination = work / name
        marker = destination / '.androidemu-input-sha256'
        if not marker.is_file() or marker.read_text().strip() != digest:
            staging = work / f'{name}.extracting'
            if staging.exists(): shutil.rmtree(staging)
            staging.mkdir()
            with tarfile.open(archive) as source:
                source.extractall(staging, filter='data')
            (staging / '.androidemu-input-sha256').write_text(digest + '\n')
            if destination.exists(): shutil.rmtree(destination)
            staging.rename(destination)
    source = work / 'source'
    compiler = work / 'toolchain/bin/arm-eabi-'
    command = ['make', '-C', str(source), 'ARCH=arm', f'CROSS_COMPILE={compiler}']
    subprocess.run(command + ['goldfish_armv7_defconfig'], check=True)
    config = (source / '.config').read_text()
    for required in ('CONFIG_HIGHMEM=y', 'CONFIG_MACH_GOLDFISH_ARMV7=y', 'CONFIG_AEABI=y',
                     'CONFIG_EXT4_FS=y', 'CONFIG_ANDROID_BINDER_IPC=y'):
        if required not in config.splitlines():
            raise RuntimeError(f'Missing kernel capability: {required}')
    env = dict(os.environ, KBUILD_BUILD_USER='androidemu', KBUILD_BUILD_HOST='builder',
               KBUILD_BUILD_TIMESTAMP='Thu Jan 1 00:00:00 UTC 1970', KBUILD_BUILD_VERSION='1')
    subprocess.run(command + [f'-j{min(os.cpu_count() or 2, 4)}', 'zImage'], env=env, check=True)
    output = ROOT / 'build/guest-kernel'
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'arch/arm/boot/zImage', output / 'goldfish-highmem.zImage')
    shutil.copy2(source / '.config', output / 'goldfish-highmem.config')
    shutil.copy2(source / 'COPYING', output / 'goldfish-kernel-COPYING.txt')
    shutil.copy2(work / 'source.tar.gz', output / 'goldfish-kernel-source.tar.gz')
    (output / 'goldfish-kernel-build.txt').write_text(
        f'AOSP Goldfish Linux 3.4.67\nSource: {INPUTS[0][1]}\nGCC: {INPUTS[1][1]}\n'
        'Build: python3 scripts/build_highmem_kernel.py\n'
        'Configuration: goldfish-highmem.config (goldfish_armv7_defconfig)\n'
        'No source patches. CONFIG_HIGHMEM=y. Goldfish MMIO starts at 0xff000000.\n')

if __name__ == '__main__':
    main()
