#!/usr/bin/env python3
"""Integration tests against the real ARM system emulator, without Android images.

QTest checks MMIO and DMA. A separate TCG run executes generated ARM instructions
through the machine's kernel loader and emits a serial marker. These are not
Android boot/Launcher tests.
"""
import os
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
QEMU = Path(os.environ.get('ANDROID51_QEMU', ROOT / 'build/qemu-host/qemu-system-arm')).resolve()

class GoldfishTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ae-qtest-')
        self.addCleanup(self.temp.cleanup)
        directory = Path(self.temp.name)
        self.system = directory / 'system.raw'
        self.userdata = directory / 'userdata.raw'
        self.cache = directory / 'cache.raw'
        self.cache.write_bytes(bytes(8192))
        self.system.write_bytes(bytes(range(256)) * 16)
        self.userdata.write_bytes(bytes(8192))
        path = directory / 'qtest.sock'
        self.log = (directory / 'qemu.log').open('w+')
        self.addCleanup(self.log.close)
        self.process = subprocess.Popen([
            str(QEMU), '-machine', 'android51,audiodev=audio', '-accel', 'qtest', '-m', '128M',
            '-display', 'none', '-nodefaults', '-nic',
            'user,model=smc91c111,ipv6=off' if self._testMethodName == 'test_nat_udp_round_trip' else 'none',
            '-audiodev', 'none,id=audio',
            '-qtest', f'unix:{path},server=on,wait=off',
            '-qmp', f'unix:{directory / "qmp.sock"},server=on,wait=off',
            '-drive', f'if=none,id=system,file={self.system},format=raw,readonly=on',
            '-drive', f'if=none,id=userdata,file={self.userdata},format=raw',
            '-drive', f'if=none,id=cache,file={self.cache},format=raw',
        ], stdout=self.log, stderr=self.log)
        self.addCleanup(self.stop)
        deadline = time.monotonic() + 10
        while not path.exists():
            if self.process.poll() is not None or time.monotonic() > deadline:
                self.log.seek(0)
                self.fail(self.log.read())
            time.sleep(0.01)
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(self.socket.close)
        self.socket.settimeout(5)
        self.socket.connect(str(path))
        self.reader = self.socket.makefile('rb')
        self.addCleanup(self.reader.close)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def cmd(self, command):
        self.socket.sendall(command.encode() + b'\n')
        while True:
            line = self.reader.readline().decode().strip()
            if line.startswith('IRQ '):
                continue
            self.assertTrue(line.startswith('OK'), line)
            return line[2:].strip()

    def read(self, address):
        return int(self.cmd(f'readl {address:#x}'), 0)

    def write(self, address, value):
        self.cmd(f'writel {address:#x} {value & 0xffffffff:#x}')

    def put(self, address, data):
        self.cmd(f'write {address:#x} {len(data)} 0x{data.hex()}')

    def get(self, address, count):
        return bytes.fromhex(self.cmd(f'read {address:#x} {count}').removeprefix('0x'))

    def test_device_enumeration(self):
        bus = 0xff001000
        self.write(bus, 0)
        devices = {}
        while self.read(bus) == 8:
            length = self.read(bus + 8)
            self.assertLess(length, 100)
            self.write(bus + 4, 0x1000)
            name = self.get(0x1000, length).decode()
            devices[name] = (self.read(bus + 16), self.read(bus + 24))
        self.assertEqual(devices['goldfish_timer'], (0xff003000, 3))
        self.assertEqual(devices['goldfish_nand'][0], 0xff030000)
        self.assertEqual(devices['qemu_pipe'][0], 0xff070000)
        self.assertIn('goldfish_events', devices)
        self.assertIn('smc91x', devices)
        self.assertNotIn('goldfish_mmc', devices)
        self.assertEqual(self.read(bus), 0)
        self.write(bus, 0)
        self.assertEqual(self.read(bus), 8)

    def test_timer_and_irq_number(self):
        pic, timer = 0xff000000, 0xff003000
        self.write(pic + 16, 3)
        self.write(timer + 12, 0)
        self.write(timer + 8, 1000000)
        self.assertEqual(self.read(pic), 0)
        self.cmd('clock_step 2000000')
        self.assertEqual(self.read(pic), 1)
        self.assertEqual(self.read(pic + 4), 3)  # IRQ index, not mask 8.
        self.write(timer + 16, 0)
        self.assertEqual(self.read(pic), 0)
        for bad in [32, 255, 0xffffffff]:
            self.write(pic + 16, bad)
        self.assertEqual(self.read(pic), 0)

    def nand(self, dev, command, address, data, size):
        base = 0xff030000
        for offset, value in [(8, dev), (0x50, address), (0x54, address >> 32),
                              (0x48, data), (0x4c, size), (0x44, command)]:
            self.write(base + offset, value)
        return self.read(base + 0x40)

    def test_nand_read_write_protection_and_batch(self):
        base = 0xff030000
        self.assertEqual(self.read(base), 1)
        self.assertEqual(self.read(base + 4), 3)
        self.assertEqual(self.nand(0, 1, 0, 0x1000, 256), 256)
        self.assertEqual(self.get(0x1000, 256), bytes(range(256)))
        self.assertEqual(self.nand(0, 2, 0, 0x1000, 256), 0)
        self.assertEqual(self.nand(1, 2, 512, 0x1000, 256), 256)
        self.assertEqual(self.nand(1, 1, 512, 0x2000, 256), 256)
        self.assertEqual(self.get(0x2000, 256), bytes(range(256)))
        descriptor = struct.pack('<6I', 1, 512, 0, 256, 0x3000, 0xdeadbeef)
        self.put(0x4000, descriptor)
        self.write(base + 0x58, 0x4000)
        self.write(base + 0x44, 6)
        result = self.get(0x4000, 24)
        self.assertEqual(result[:20], descriptor[:20])
        self.assertEqual(struct.unpack('<I', result[20:])[0], 256)
        self.assertEqual(self.get(0x3000, 256), bytes(range(256)))
        self.assertEqual(self.nand(99, 1, 0, 0x1000, 256), 0)
        self.assertEqual(self.nand(1, 1, 0, 0xfffffffc, 256), 0)
        self.stop()
        self.assertEqual(self.userdata.read_bytes()[512:768], bytes(range(256)))
        self.assertEqual(self.system.read_bytes(), bytes(range(256)) * 16)

    def test_cache_is_third_partition_and_persists_independently(self):
        self.assertEqual(self.read(0xff030004), 3)
        payload = b'CACHE-PERSIST' + bytes(512 - 13)
        self.put(0x1000, payload)
        self.assertEqual(self.nand(2, 2, 4096, 0x1000, len(payload)), len(payload))
        self.assertEqual(self.nand(2, 1, 4096, 0x2000, len(payload)), len(payload))
        self.assertEqual(self.get(0x2000, len(payload)), payload)
        self.stop()
        self.assertEqual(self.cache.read_bytes()[4096:4608], payload)
        self.assertEqual(self.userdata.read_bytes(), bytes(8192))
        self.assertEqual(self.system.read_bytes(), bytes(range(256)) * 16)

    def test_dma_cannot_reenter_mmio(self):
        # A DMA pointer is untrusted. It must never recurse into a device's
        # command register or accept mapped MMIO as a valid payload buffer.
        self.assertEqual(self.nand(1, 1, 0, 0xff030044, 4), 0)
        self.assertEqual(self.nand(1, 2, 0, 0xff030040, 4), 0)
        self.write(0xff030058, 0xff030030)
        self.write(0xff030044, 6)
        self.assertEqual(self.read(0xff030040), 0)
        self.write(0xff070018, 0xff070000)
        self.write(0xff070020, 0)
        self.assertEqual(self.read(0xff030004), 3)

    def test_audio_buffer_submission_and_irq(self):
        base, pic = 0xff004000, 0xff000000
        self.write(pic + 16, 16)
        self.write(base + 4, 3)
        self.assertEqual(self.read(pic + 4), 16)
        self.assertEqual(self.read(base), 3)
        self.assertEqual(self.read(pic), 0)
        self.put(0x9000, bytes(4096))
        self.write(base + 8, 0x9000)
        self.write(base + 16, 4096)
        self.cmd('clock_step 100000000')
        self.assertEqual(self.read(base) & 1, 1)
        self.assertEqual(self.read(base + 24), 0)  # No capture device.

    def test_framebuffer_interrupt_and_capabilities(self):
        fb = 0xff040000
        self.assertEqual((self.read(fb), self.read(fb + 4)), (540, 960))
        self.assertEqual(self.read(fb + 0x24), 4)
        self.write(fb + 12, 3)
        self.write(fb + 16, 0x100000)
        self.cmd('clock_step 20000000')
        self.assertEqual(self.read(fb + 8), 3)
        self.assertEqual(self.read(fb + 8), 0)
        events = 0xff050000
        self.write(events, 0)
        name = bytes(int(self.cmd(f'readb {events + 8 + i:#x}'), 0) for i in range(self.read(events + 4)))
        self.assertEqual(name, b'qwerty2')
        self.write(events, 0x10002)
        self.assertEqual(self.read(events + 4), 0)  # No relative mouse axes.
        self.write(events, 0x10001)
        self.assertEqual(int(self.cmd(f'readb {events + 8 + 30 // 8:#x}'), 0) & (1 << (30 % 8)), 0)
        self.assertNotEqual(int(self.cmd(f'readb {events + 8 + 102 // 8:#x}'), 0) & (1 << (102 % 8)), 0)
        self.write(events, 0x20003)
        self.assertGreaterEqual(self.read(events + 4), 58 * 16)
        self.assertEqual(self.read(events + 8 + 47 * 16 + 4), 9)
        self.assertEqual(self.read(0xff060018), 100)

    def test_rgb565_scanout_stride_page_flip_and_blank(self):
        fb, width, height = 0xff040000, 540, 960
        size = width * height * 2
        # The last frame fits exactly at the end of RAM. A 32-bpp reader
        # would reject this valid frame or read beyond guest RAM.
        front, back = 0x100000, 128 * 1024 * 1024 - size
        colors = {(0, 0): (0xf800, b'\xff\0\0'),
                  (1, 0): (0x07e0, b'\0\xff\0'),
                  (width - 1, 0): (0x001f, b'\0\0\xff'),
                  (0, 1): (0xffff, b'\xff\xff\xff'),
                  (0, height - 1): (0x001f, b'\0\0\xff'),
                  (width - 1, height - 1): (0xf800, b'\xff\0\0')}
        for (x, y), (packed, _) in colors.items():
            self.put(front + (y * width + x) * 2, struct.pack('<H', packed))
        self.put(back, struct.pack('<H', 0x07e0))
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as qmp:
            qmp.settimeout(5)
            qmp.connect(str(Path(self.temp.name) / 'qmp.sock'))
            with qmp.makefile('rb') as stream:
                self.assertIn('QMP', json.loads(stream.readline()))
                def command(name, arguments=None):
                    qmp.sendall((json.dumps({'execute': name, 'arguments': arguments or {}}) + '\n').encode())
                    while True:
                        response = json.loads(stream.readline())
                        if 'event' in response: continue
                        self.assertIn('return', response)
                        return response['return']
                command('qmp_capabilities')
                def screenshot():
                    path = Path(self.temp.name) / 'frame.ppm'
                    command('screendump', {'filename': str(path)})
                    with path.open('rb') as ppm:
                        self.assertEqual(ppm.readline(), b'P6\n')
                        self.assertEqual(ppm.readline(), b'540 960\n')
                        self.assertEqual(ppm.readline(), b'255\n')
                        return ppm.read()
                self.write(fb + 12, 3)
                self.write(fb + 16, front)
                self.cmd('clock_step 20000000')
                image = screenshot()
                for (x, y), (_, rgb) in colors.items():
                    offset = (y * width + x) * 3
                    self.assertEqual(image[offset:offset+3], rgb, (x, y))
                self.assertEqual(image[width*3+3:width*3+6], bytes(3))
                self.write(fb + 16, back)
                self.cmd('clock_step 20000000')
                self.assertEqual(self.read(fb + 8) & 2, 2)
                image = screenshot()
                self.assertEqual(image[:3], b'\0\xff\0')
                self.assertEqual(image[3:], bytes(width * height * 3 - 3))
                self.write(fb + 24, 1)
                self.cmd('clock_step 20000000')
                self.assertEqual(screenshot(), bytes(width * height * 3))
                self.write(fb + 24, 0)
                self.cmd('clock_step 20000000')
                self.assertEqual(screenshot(), image)
                # Same content at another address must still acknowledge flips.
                self.write(fb + 16, back)
                self.cmd('clock_step 20000000')
                self.assertEqual(self.read(fb + 8) & 2, 2)
                self.assertEqual(screenshot(), image)

    def pipe(self, command, channel=1, payload=None, size=0):
        base = 0xff070000
        self.write(base + 8, channel)
        if payload is not None:
            self.put(0x8000, payload)
            size = len(payload)
        self.write(base + 12, size)
        self.write(base + 16, 0x8000)
        self.write(base, command)
        return self.read(base + 4)

    def test_nat_udp_round_trip(self):
        # Actual guest Ethernet -> libslirp -> host UDP -> guest, no Internet
        # dependency and no host-forwarded/listening QEMU port.
        base = 0xff020000
        def write(offset, value, kind='w'):
            self.cmd(f'write{kind} {base + offset:#x} {value:#x}')
        def read(offset, kind='w'):
            return int(self.cmd(f'read{kind} {base + offset:#x}'), 0)
        write(14, 0); write(0, 0x81); write(4, 0x300)
        write(14, 1); write(12, 0x800)
        write(14, 2)
        def send(frame):
            frame = frame.ljust(64, b'\0')
            if len(frame) % 2: frame += b'\0'
            write(0, 0x20)
            packet = read(3, 'b')
            self.assertLess(packet, 4)
            write(2, packet, 'b'); write(6, 0x4000)
            data = struct.pack('<HH', 0, len(frame) + 6) + frame + b'\0\0'
            for offset in range(0, len(data), 2):
                write(8, int.from_bytes(data[offset:offset + 2], 'little'))
            write(0, 0xc0)
        def receive():
            deadline = time.monotonic() + 3
            while read(4) & 0x8000:
                self.assertLess(time.monotonic(), deadline, 'NAT response timed out')
                time.sleep(0.01)
            write(6, 0xe000)
            status, length = read(8), read(8)
            self.assertLess(length, 2048)
            data = b''.join(read(8).to_bytes(2, 'little') for _ in range((length - 4) // 2))
            write(0, 0x80)
            return data[:length - 6 + bool(status & 0x1000)]
        mac = bytes.fromhex('525400123456')
        guest, gateway = socket.inet_aton('10.0.2.15'), socket.inet_aton('10.0.2.2')
        send(b'\xff' * 6 + mac + b'\x08\x06' + struct.pack('!HHBBH', 1, 0x800, 6, 4, 1) +
             mac + guest + bytes(6) + gateway)
        arp = receive()
        self.assertEqual(arp[12:14], b'\x08\x06')
        self.assertEqual(arp[20:22], b'\0\x02')
        gateway_mac = arp[22:28]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host:
            host.bind(('127.0.0.1', 0)); host.settimeout(3)
            payload = b'ANDROIDEMU_NAT_TEST'
            udp = struct.pack('!4H', 42000, host.getsockname()[1], 8 + len(payload), 0) + payload
            ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp), 1, 0, 64, 17, 0, guest, gateway)
            checksum = sum(struct.unpack('!10H', ip))
            while checksum >> 16: checksum = (checksum & 65535) + (checksum >> 16)
            ip = ip[:10] + struct.pack('!H', checksum ^ 65535) + ip[12:]
            send(gateway_mac + mac + b'\x08\0' + ip + udp)
            received, address = host.recvfrom(2048)
            self.assertEqual(received, payload)
            host.sendto(payload, address)
            response = receive()
            self.assertEqual(response[12:14], b'\x08\0')
            self.assertEqual(response[42:42 + len(payload)], payload)

    def test_gsm_data_registration_and_context(self):
        self.assertEqual(self.pipe(1), 0)
        invite = b'pipe:qemud:gsm\0'
        self.assertEqual(self.pipe(4, payload=invite), len(invite))
        def command(text):
            data = text.encode() + b'\r'
            # Real RIL writes can split at any byte boundary.
            self.assertEqual(self.pipe(4, payload=data[:3]), 3)
            self.assertEqual(self.pipe(4, payload=data[3:]), len(data) - 3)
            size = self.pipe(6, size=4096)
            self.assertLess(size, 4096)
            return self.get(0x8000, size)
        self.assertIn(b'+CPIN: READY', command('AT+CPIN?'))
        self.assertIn(b'310260000000000', command('AT+CIMI'))
        self.assertIn(b'OK', command('AT+CGREG=2'))
        self.assertIn(b'+CGREG: 2,1,"0001","0001",3', command('AT+CGREG?'))
        self.assertIn(b'+CRSM: 144,0,00000003', command('AT+CRSM=176,28589,0,0,4'))
        self.assertIn(b'OK', command('AT+CGDCONT=1,"IP","internet",,0,0'))
        self.assertIn(b'+CGACT: 1,0', command('AT+CGACT?'))
        self.assertIn(b'OK', command('ATD*99***1#'))
        self.assertIn(b'OK', command('AT+CGDCONT=1,"IPV4V6","dual",,0,0'))
        self.assertIn(b'"IP","dual","10.0.2.15"', command('AT+CGDCONT?'))
        self.assertIn(b'OK', command('AT+CGDCONT=1,"IP","internet",,0,0'))
        self.assertIn(b'+CGACT: 1,1', command('AT+CGACT?'))
        self.assertIn(b'"internet","10.0.2.15"', command('AT+CGDCONT?'))
        self.assertIn(b'ERROR', command('AT+UNSUPPORTED'))
        self.assertIn(b'+CGREG: 0', command('AT+CFUN=0'))
        self.assertIn(b'+CGACT: 1,0', command('AT+CGACT?'))
        self.assertIn(b'ERROR', command('ATD*99***1#'))
        self.assertIn(b'+CGREG: 1', command('AT+CFUN=1'))
        self.assertIn(b'OK', command('ATD*99***1#'))
        self.assertEqual(self.pipe(2), 0)

    def test_gsm_backpressure_and_oversized_command(self):
        self.assertEqual(self.pipe(1), 0)
        invite = b'pipe:qemud:gsm\0'
        self.assertEqual(self.pipe(4, payload=invite), len(invite))
        payload = b'AT\r' * 2000
        sent = self.pipe(4, payload=payload)
        self.assertGreater(sent, 0)
        self.assertLess(sent, len(payload))
        self.assertEqual(self.pipe(3) & 2, 0)
        size = self.pipe(6, size=8192)
        replies = self.get(0x8000, size)
        self.assertEqual(self.pipe(3) & 2, 2)
        rest = payload[sent:]
        self.assertEqual(self.pipe(4, payload=rest), len(rest))
        size = self.pipe(6, size=8192)
        replies += self.get(0x8000, size)
        self.assertEqual(replies.count(b'OK'), 2000)
        self.assertEqual(self.pipe(4, payload=b'A' * 1024), 0xfffffffc)
        self.assertEqual(self.pipe(2), 0)

    @unittest.skipUnless(os.environ.get('ANDROID51_TEST_GPU') == '1', 'GPU-enabled QEMU build required')
    def test_gpu_pipe_fragmented_handshake_and_response(self):
        self.assertEqual(self.pipe(1), 0)
        name = b'pipe:opengles\0'
        self.assertEqual(self.pipe(4, payload=name), len(name))
        payload = struct.pack('<III', 0, 10000, 8)  # client flags, rcGetRendererVersion
        for fragment in (payload[:1], payload[1:7], payload[7:]):
            self.assertEqual(self.pipe(4, payload=fragment), len(fragment))
        self.assertEqual(self.pipe(7), 0)
        deadline = time.monotonic() + 5
        channel = self.read(0xff070008)
        while channel == 0:
            self.assertLess(time.monotonic(), deadline, 'GPU readiness interrupt timeout')
            time.sleep(0.001)
            channel = self.read(0xff070008)
        self.assertEqual(channel, 1)
        self.assertEqual(self.read(0xff070014) & 2, 2)
        self.assertEqual(self.pipe(6, size=4), 4)
        self.assertEqual(self.get(0x8000, 4), struct.pack('<I', 1))
        self.assertEqual(self.pipe(2), 0)

    def test_pipe_boot_properties_partial_frames_and_wakeup(self):
        self.assertEqual(self.pipe(1), 0)
        invite = b'pipe:qemud:boot-properties\0'
        self.assertEqual(self.pipe(4, payload=invite), len(invite))
        self.assertEqual(self.pipe(7), 0)
        self.assertEqual(self.pipe(4, payload=b'00'), 2)
        self.assertEqual(self.pipe(4, payload=b'04list'), 6)
        self.assertEqual(self.read(0xff070008), 1)
        self.assertEqual(self.read(0xff070014), 2)
        length = self.pipe(6, size=4096)
        response = self.get(0x8000, length)
        properties = []
        while response:
            length = int(response[:4], 16)
            properties.append(response[4:4 + length])
            response = response[4 + length:]
        self.assertIn(b'qemu.gles=1' if os.environ.get('ANDROID51_TEST_GPU') == '1' else b'qemu.gles=0', properties)
        self.assertEqual(properties[-1], b'\0')
        self.assertEqual(self.pipe(6, size=1), 0xfffffffe)
        self.assertEqual(self.pipe(2), 0)
        self.assertEqual(self.pipe(1, channel=2), 0)
        self.assertEqual(self.pipe(4, channel=2, payload=b'pipe:does-not-exist\0'), 0xffffffff)

class TCGBootTests(unittest.TestCase):
    def test_arm_boot_loader_and_serial(self):
        marker = b'ANDROID51_TCG_OK\n'
        # ARM32: ldr r0, literal; [mov r1,#char; strb r1,[r0]]; b .; literal.
        words = [0]
        for char in marker:
            words.extend([0xe3a01000 | char, 0xe5c01000])
        words.append(0xeafffffe)
        words[0] = 0xe59f0000 | (len(words) * 4 - 8)
        words.append(0xff002000)
        with tempfile.TemporaryDirectory(prefix='ae-tcg-') as folder:
            directory = Path(folder)
            kernel = directory / 'probe.bin'
            kernel.write_bytes(struct.pack('<' + 'I' * len(words), *words))
            serial = directory / 'serial.log'
            with (directory / 'qemu.log').open('w+') as log:
                process = subprocess.Popen([str(QEMU), '-machine', 'android51,audiodev=audio', '-accel', 'tcg,tb-size=128,split-wx=on', '-m', '128M',
                    '-display', 'none', '-nodefaults', '-nic', 'user,model=smc91c111,ipv6=off', '-audiodev', 'none,id=audio', '-serial', f'file:{serial}', '-kernel', str(kernel)], stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline and process.poll() is None:
                        if serial.exists() and marker in serial.read_bytes():
                            break
                        time.sleep(0.02)
                    log.seek(0)
                    self.assertTrue(serial.exists() and marker in serial.read_bytes(), log.read())
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()

if __name__ == '__main__':
    unittest.main(verbosity=2)
