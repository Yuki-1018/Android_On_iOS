#!/usr/bin/env python3
"""Create an empty, Linux 3.4-compatible ext4 cache (no Android OS content)."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SIZE = 64 * 1024 * 1024
BLOCK = 4096


def tool(name):
    found = shutil.which(name)
    if found:
        return found
    if shutil.which('brew'):
        prefix = subprocess.check_output(['brew', '--prefix', 'e2fsprogs'], text=True).strip()
        candidate = Path(prefix) / 'sbin' / name
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError('Install e2fsprogs to generate the empty cache filesystem')


def create(destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='androidemu-cache-') as temporary:
        raw = Path(temporary) / 'cache.raw'
        with raw.open('wb') as output:
            output.truncate(SIZE)
        # Explicit features avoid newer defaults such as metadata_csum/64bit
        # that the Android 5.1 goldfish kernel and e2fsck cannot read.
        subprocess.run([tool('mke2fs'), '-q', '-F', '-t', 'ext4', '-b', str(BLOCK),
                        '-I', '256', '-m', '0', '-L', 'cache',
                        '-O', 'none,has_journal,ext_attr,filetype,extent,sparse_super,large_file',
                        '-E', 'lazy_itable_init=0,lazy_journal_init=0,root_owner=0:0', str(raw)], check=True)
        subprocess.run([tool('e2fsck'), '-fn', str(raw)], check=True, capture_output=True)
        chunks = []
        with raw.open('rb') as source:
            for _ in range(SIZE // BLOCK):
                data = source.read(BLOCK)
                zero = not any(data)
                if chunks and chunks[-1][0] == zero:
                    chunks[-1][1] += 1
                    if not zero:
                        chunks[-1][2].extend(data)
                else:
                    chunks.append([zero, 1, bytearray() if zero else bytearray(data)])
        staged = destination.with_suffix('.tmp')
        try:
            with staged.open('wb') as output:
                output.write(struct.pack('<I4H4I', 0xed26ff3a, 1, 0, 28, 12, BLOCK, SIZE // BLOCK, len(chunks), 0))
                for zero, blocks, data in chunks:
                    output.write(struct.pack('<2H2I', 0xcac3 if zero else 0xcac1, 0, blocks, 12 + len(data)))
                    output.write(data)
            os.replace(staged, destination)
        finally:
            staged.unlink(missing_ok=True)
    return destination


if __name__ == '__main__':
    print(create(ROOT / 'build/cache-template.sparse'))
