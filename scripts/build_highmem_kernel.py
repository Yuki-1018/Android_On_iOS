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
import time
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE_REV = '880d9af358076df842377facd4b900f6bbecc783'
GCC_REV = '26e93f6af47f7bd3a9beb5c102a5f45e19bfa38a'
INPUTS = (
    ('source', 'https://android.googlesource.com/kernel/goldfish', SOURCE_REV),
    ('toolchain', 'https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8', GCC_REV),
)

def verified_archive(path, repository, revision):
    """Verify Git objects at an exact commit, then generate our own archive.

    Gitiles +archive responses are generated tar/gzip representations, not
    immutable release assets. Their compressed-byte digest is not a source ID.
    Never trust an old downloaded archive merely because it exists locally.
    """
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('A full pinned commit ID is required')
    path = Path(path).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    checkout = path.with_suffix('.git')
    if not checkout.exists():
        subprocess.run(['git', 'init', '--bare', '--quiet', str(checkout)], check=True)
    git = ['git', '-C', str(checkout)]
    present = subprocess.run(git + ['cat-file', '-e', revision + '^{commit}'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not present:
        for attempt in range(4):
            try:
                subprocess.run(git + ['-c', 'fetch.fsckObjects=true', 'fetch', '--no-tags',
                                     '--depth=1', repository, revision], check=True, timeout=300)
                break
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                if attempt == 3: raise
                time.sleep(2 ** attempt)
    actual = subprocess.check_output(git + ['rev-parse', '--verify', revision + '^{commit}'], text=True).strip()
    if actual != revision:
        raise RuntimeError(f'Commit mismatch: expected {revision}, got {actual}')
    subprocess.run(git + ['fsck', '--full', '--no-dangling', revision], check=True)
    # Keep the verified commit reachable across cache reuse and Git GC.
    subprocess.run(git + ['update-ref', 'refs/heads/pinned', revision], check=True)
    partial = path.with_suffix('.partial')
    try:
        subprocess.run(git + ['archive', '--format=tar.gz', '--output=' + str(partial), revision], check=True)
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)
    # This local digest invalidates extracted caches; provenance is the commit
    # and its verified tree/blob objects, not this generated gzip checksum.
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    work = ROOT / 'build/highmem'
    work.mkdir(parents=True, exist_ok=True)
    for name, url, revision in INPUTS:
        archive = work / f'{name}.tar.gz'
        digest = verified_archive(archive, url, revision)
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
        f'AOSP Goldfish Linux 3.4.67\nSource: {INPUTS[0][1]} @ {SOURCE_REV}\nGCC: {INPUTS[1][1]} @ {GCC_REV}\n'
        'Build: python3 scripts/build_highmem_kernel.py\n'
        'Configuration: goldfish-highmem.config (goldfish_armv7_defconfig)\n'
        'No source patches. CONFIG_HIGHMEM=y. Goldfish MMIO starts at 0xff000000.\n')

if __name__ == '__main__':
    main()
