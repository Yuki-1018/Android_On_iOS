#!/usr/bin/env python3
"""Exercise the SAME in-process ABI used by iOS, inside a disposable process.

Generated ARM instructions program the framebuffer, audio and serial MMIO. This
is a real TCG/shared-library lifecycle test, not an Android OS boot claim.
"""
import ctypes as C
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / 'build/qemu-host/libqemu-arm-softmmu.so'


def probe_code():
    words, literals, fixups = [], [], []
    def load(reg, value):
        fixups.append((len(words), len(literals), reg))
        words.append(0)
        literals.append(value)
    def write(address, value):
        load(0, address); load(1, value); words.append(0xe5801000)
    write(0x100000, 0xff1267ab)  # A real BGRA framebuffer pixel.
    write(0xff040010, 0x100000)
    write(0xff004004, 3)
    write(0xff004008, 0x200000)
    write(0xff004010, 4096)
    for byte in b'EMBEDDED_TCG_OK\n':
        write(0xff002000, byte)
    # Open the stock adbd pipe and exercise its fragmented accept/start handshake.
    def pipe_send(payload):
        padded = payload + bytes((-len(payload)) % 4)
        for offset in range(0, len(padded), 4):
            write(0x300000 + offset, int.from_bytes(padded[offset:offset+4], 'little'))
        write(0xff07000c, len(payload)); write(0xff070010, 0x300000); write(0xff070000, 4)
    write(0xff070008, 1); write(0xff070000, 1)
    pipe_send(b'pipe:qemud:adb:5555\0')
    pipe_send(b'ac'); pipe_send(b'cept')
    write(0xff07000c, 2); write(0xff070010, 0x300100); write(0xff070000, 6)
    pipe_send(b'start'); pipe_send(b'FROM_ANDROID')
    # Poll guest READ into actual framebuffer RAM, proving host -> guest transfer.
    write(0xff07000c, 9); write(0xff070010, 0x100004)
    loop = len(words)
    write(0xff070000, 6)
    distance = loop - (len(words) + 2)
    words.append(0xea000000 | (distance & 0x00ffffff))
    for position, literal, reg in fixups:
        offset = (len(words) + literal - position) * 4 - 8
        assert 0 <= offset < 4096
        words[position] = 0xe59f0000 | reg << 12 | offset
    return struct.pack('<' + 'I' * (len(words) + len(literals)), *(words + literals))


def child(directory):
    directory = Path(directory)
    lib = C.CDLL(str(LIBRARY))
    libc = C.CDLL(None, use_errno=True)
    libc.mmap.argtypes = [C.c_void_p, C.c_size_t, C.c_int, C.c_int, C.c_int, C.c_long]
    libc.mmap.restype = C.c_void_p
    arena_size = 16 * 1024 * 1024
    arena_fd = os.memfd_create('android51-test-arena', os.MFD_CLOEXEC)
    os.ftruncate(arena_fd, arena_size)
    rw = libc.mmap(None, arena_size, 3, 1, arena_fd, 0)  # Shared RW.
    rx = libc.mmap(None, arena_size, 5, 1, arena_fd, 0)  # Same bytes, RX alias.
    assert rw not in (None, C.c_void_p(-1).value) and rx not in (None, C.c_void_p(-1).value)
    lib.android51_tcg_set_region.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    lib.android51_tcg_set_region.restype = C.c_bool
    assert not lib.android51_tcg_set_region(rw, rw, arena_size)
    assert lib.android51_tcg_set_region(rw, rx, arena_size)
    assert not lib.android51_tcg_set_region(rw, rx, arena_size)
    class Event(C.Structure):
        _fields_ = [('type', C.c_uint16), ('code', C.c_uint16), ('value', C.c_int32)]
    Frame = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32)
    Input = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.POINTER(Event), C.c_size_t)
    Bytes = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t)
    State = C.CFUNCTYPE(None, C.c_void_p, C.c_int)
    class Host(C.Structure):
        _fields_ = [('abi', C.c_uint32), ('size', C.c_uint32), ('opaque', C.c_void_p),
                    ('frame', Frame), ('input', Input), ('pcm', Bytes), ('serial', Bytes), ('state', State)]
    results = {'states': [], 'serial': '', 'frames': 0, 'pcm': 0, 'inputPolls': 0, 'pixel': None, 'adbPixel': '', 'adbFromGuest': '', 'panel': None}
    ready = threading.Event()
    @Frame
    def frame(ctx, data, stride, x, y, width, height):
        results['frames'] += 1
        if results['panel'] is None: results['panel'] = [width, height]
        if y == 0:
            results['pixel'] = C.string_at(data, 4).hex()
            results['adbPixel'] = C.string_at(C.addressof(data.contents) + 4, 9).hex()
    @Input
    def inputs(ctx, events, capacity):
        results['inputPolls'] += 1
        if results['inputPolls'] == 1:
            events[0] = Event(1, 172, 1); events[1] = Event(0, 0, 0)
            events[2] = Event(1, 172, 0); events[3] = Event(0, 0, 0)
            return 4
        return 0
    @Bytes
    def pcm(ctx, data, length): results['pcm'] += length
    @Bytes
    def serial(ctx, data, length): results['serial'] += C.string_at(data, length).decode('ascii')
    @State
    def state(ctx, value):
        results['states'].append(value)
        if value == 1: ready.set()
    host = Host(1, C.sizeof(Host), None, frame, inputs, pcm, serial, state)
    lib.android51_host_run.argtypes = [C.c_int, C.POINTER(C.c_char_p), C.POINTER(Host)]
    lib.android51_host_run.restype = C.c_int
    lib.android51_host_pause.argtypes = [C.c_bool]
    lib.android51_host_stop.argtypes = []
    lib.android51_adb_connected.restype = C.c_bool
    lib.android51_adb_disconnect.argtypes = []
    lib.android51_adb_read.argtypes = [C.c_void_p, C.c_size_t]
    lib.android51_adb_read.restype = C.c_size_t
    lib.android51_adb_write.argtypes = [C.c_void_p, C.c_size_t]
    lib.android51_adb_write.restype = C.c_size_t
    # Stop from another thread while paused: this must not depend on guest time.
    def control():
        if not ready.wait(8): return
        limit = time.monotonic() + 5
        buffer = C.create_string_buffer(64)
        while time.monotonic() < limit:
            count = lib.android51_adb_read(buffer, 64)
            if count:
                results['adbFromGuest'] += buffer.raw[:count].decode('ascii')
                if results['adbFromGuest'] == 'FROM_ANDROID': break
            time.sleep(0.005)
        results['adbWritten'] = lib.android51_adb_write(b'BIDIR_ADB', 9)
        time.sleep(0.2)
        lib.android51_adb_disconnect()
        limit = time.monotonic() + 2
        while lib.android51_adb_connected() and time.monotonic() < limit: time.sleep(0.005)
        results['adbDisconnected'] = not lib.android51_adb_connected()
        lib.android51_host_pause(True)
        time.sleep(0.1)
        lib.android51_host_pause(False)
        time.sleep(0.1)
        lib.android51_host_pause(True)
        time.sleep(0.1)
        lib.android51_host_stop()
    worker = threading.Thread(target=control, daemon=True); worker.start()
    args = ['embedded-probe', '-machine', 'android51,audiodev=audio,width=540,height=1170',
            '-m', '128M', '-accel', 'tcg,split-wx=on,tb-size=128', '-nodefaults',
            '-display', 'none', '-serial', 'null', '-monitor', 'none', '-nic', 'none',
            '-audiodev', 'none,id=audio', '-kernel', str(directory / 'probe.bin')]
    argv = (C.c_char_p * (len(args) + 1))(*[a.encode() for a in args], None)
    def execute():
        results['exit'] = lib.android51_host_run(len(args), argv, C.byref(host))
    engine_thread = threading.Thread(target=execute, name='android51-owned-thread')
    engine_thread.start()
    engine_thread.join(timeout=10)
    assert not engine_thread.is_alive(), 'QEMU owned thread did not exit' 
    lib.android51_host_metric.argtypes = [C.c_uint]
    lib.android51_host_metric.restype = C.c_uint64
    results['tcgCapacity'] = lib.android51_host_metric(1)
    results['externalArenaUsed'] = any(C.string_at(rw, 4096))
    results['secondRun'] = lib.android51_host_run(len(args), argv, C.byref(host))
    worker.join(timeout=2)
    (directory / 'result.json').write_text(json.dumps(results))


class EmbeddedTests(unittest.TestCase):
    def test_embedded_lifecycle_and_real_device_callbacks(self):
        with tempfile.TemporaryDirectory(prefix='ae-embedded-') as folder:
            path = Path(folder)
            (path / 'probe.bin').write_bytes(probe_code())
            process = subprocess.run([sys.executable, __file__, '--child', folder], timeout=15, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            result = json.loads((path / 'result.json').read_text())
            self.assertEqual(result['exit'], 0)
            self.assertTrue(result['externalArenaUsed'])
            self.assertGreater(result['tcgCapacity'], 0)
            self.assertLessEqual(result['tcgCapacity'], 16 * 1024 * 1024)
            self.assertEqual(result['secondRun'], -1)
            self.assertEqual(result['states'], [1, 2, 3, 2, 4])
            self.assertIn('EMBEDDED_TCG_OK', result['serial'])
            self.assertGreater(result['frames'], 0)
            self.assertEqual(result['panel'], [540, 1170])
            self.assertEqual(result['pixel'], 'ab6712ff')
            self.assertEqual(result['pcm'], 4096)
            self.assertEqual(result['adbFromGuest'], 'FROM_ANDROID')
            self.assertEqual(result['adbWritten'], 9)
            self.assertTrue(result['adbDisconnected'])
            self.assertEqual(result['adbPixel'], b'BIDIR_ADB'.hex())
            self.assertGreater(result['inputPolls'], 1)

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--child': child(sys.argv[2])
    else: unittest.main(verbosity=2)
