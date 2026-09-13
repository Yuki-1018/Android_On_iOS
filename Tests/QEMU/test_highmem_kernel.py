#!/usr/bin/env python3
"""Boot the built HIGHMEM kernel and touch 1 GiB from an ARM userspace init.

This checks real kernel/TCG/highmem operation, not Android framework startup.
"""
import gzip
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
QEMU = Path(os.environ.get('ANDROID51_QEMU', ROOT / 'build/qemu-host/qemu-system-arm'))
KERNEL = ROOT / 'build/guest-kernel/goldfish-highmem.zImage'
COMPILER = ROOT / 'build/highmem/toolchain/bin/arm-eabi-gcc'

INIT = r'''
static long call(long nr, long a, long b, long c, long d, long e, long f) {
    register long r0 asm("r0")=a, r1 asm("r1")=b, r2 asm("r2")=c;
    register long r3 asm("r3")=d, r4 asm("r4")=e, r5 asm("r5")=f, r7 asm("r7")=nr;
    asm volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory", "cc");
    return r0;
}
void _start(void) {
    const unsigned bytes = 1024U*1024U*1024U;
    volatile unsigned *p = (void *)call(192, 0, bytes, 3, 0x22, -1, 0);
    int good = (unsigned long)p < 0xfffff001UL;
    if (good) {
        for (unsigned i=0; i<bytes/4; i+=1024) p[i]=i ^ 0x5a5a1234U;
        for (unsigned i=0; i<bytes/4; i+=1024) if (p[i] != (i ^ 0x5a5a1234U)) good=0;
    }
    const char *message = good ? "HIGHMEM_USER_OK\n" : "HIGHMEM_USER_FAILED\n";
    call(4, 1, (long)message, good ? 16 : 20, 0, 0, 0);
    for (;;) call(29, 0, 0, 0, 0, 0, 0);
}
'''

def initramfs(binary):
    archive = bytearray()
    for inode, (name, mode, data, major, minor) in enumerate([
        ('dev', 0o040755, b'', 0, 0), ('dev/console', 0o020600, b'', 5, 1),
        ('init', 0o100755, binary, 0, 0), ('TRAILER!!!', 0, b'', 0, 0),
    ], 1):
        encoded = name.encode() + b'\0'
        fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(encoded), 0]
        archive += ('070701' + ''.join(f'{v:08x}' for v in fields)).encode() + encoded
        archive += bytes(-len(archive) % 4)
        archive += data
        archive += bytes(-len(archive) % 4)
    return gzip.compress(archive, mtime=0)

class HighmemKernelTests(unittest.TestCase):
    def test_mmio_overlap_is_rejected(self):
        result = subprocess.run([str(QEMU), '-machine', 'android51', '-m', '4096',
                                 '-nodefaults', '-display', 'none', '-nic', 'none'],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('top 16 MiB reserved for MMIO', result.stderr)

    def test_boot_and_userspace_highmem(self):
        self.assertTrue(KERNEL.is_file(), 'Build the HIGHMEM kernel first')
        with tempfile.TemporaryDirectory(prefix='ae-highmem-') as folder:
            root = Path(folder)
            (root / 'init.c').write_text(INIT)
            subprocess.run([str(COMPILER), '-O2', '-std=gnu99', '-march=armv7-a', '-marm', '-nostdlib', '-static',
                            '-fno-stack-protector', '-Wl,-e,_start', '-o', str(root / 'init'), str(root / 'init.c')], check=True)
            (root / 'initrd').write_bytes(initramfs((root / 'init').read_bytes()))
            for ram in (2048, 4080):
                with self.subTest(ram=ram):
                    serial = root / f'serial-{ram}.log'
                    with (root / 'qemu.log').open('w+') as log:
                        process = subprocess.Popen([str(QEMU), '-machine', 'android51,audiodev=audio',
                            '-accel', 'tcg,tb-size=128', '-m', str(ram), '-nodefaults', '-display', 'none',
                            '-nic', 'none', '-audiodev', 'none,id=audio', '-serial', f'file:{serial}',
                            '-kernel', str(KERNEL), '-initrd', str(root / 'initrd'),
                            '-append', 'console=ttyS0 rdinit=/init panic=-1'], stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + 90
                            output = ''
                            while time.monotonic() < deadline and process.poll() is None:
                                output = serial.read_text(errors='replace') if serial.exists() else ''
                                if 'HIGHMEM_USER_' in output or 'Kernel panic' in output: break
                                time.sleep(0.1)
                            log.seek(0)
                            self.assertIn(f'= {ram}MB total', output, output + log.read())
                            self.assertIn('HIGHMEM_USER_OK', output, output)
                            print(f'{ram} MiB: kernel boot and 1 GiB userspace write/read passed', flush=True)
                        finally:
                            process.terminate()
                            try: process.wait(timeout=5)
                            except subprocess.TimeoutExpired: process.kill(); process.wait()

if __name__ == '__main__':
    unittest.main(verbosity=2)
