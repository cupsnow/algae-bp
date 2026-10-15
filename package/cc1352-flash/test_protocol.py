#!/usr/bin/env python3
"""PTY ROM emulator: exercise full flash, fragmented replies and failure paths."""
import errno
import os
import pty
import select
import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path

TOOL = Path(__file__).with_name('cc1352-flash').resolve()
SIZE = 704 * 1024


def run(mode):
    master, slave = pty.openpty()
    with tempfile.TemporaryDirectory() as tmp:
        image = bytes(range(256)) * 3
        path = Path(tmp) / 'image.bin'
        path.write_bytes(image)
        expected = image + b'\xff' * (SIZE - len(image))
        flash = bytearray(expected if mode == 'verify' else b'\x00' * SIZE)
        args = [str(TOOL), '--manual', '-p', os.ttyname(slave)]
        if mode == 'verify':
            args += ['--verify-only']
        with tempfile.TemporaryFile() as output:
            proc = subprocess.Popen(args + [str(path)], stdout=output, stderr=output)
            deadline = time.monotonic() + 30

            def read(n):
                data = b''
                while len(data) < n:
                    assert time.monotonic() < deadline, 'emulator deadline'
                    ready, _, _ = select.select([master], [], [], 0.1)
                    if not ready:
                        assert proc.poll() is None, 'unexpected client exit'
                        continue
                    data += os.read(master, n - len(data))
                return data

            def reply(payload):
                # Leading idle zeros and fragmented response headers/payload.
                packet = bytes([0, len(payload) + 2, sum(payload) & 255]) + payload
                if mode == 'checksum':
                    packet = packet[:2] + bytes([packet[2] ^ 1]) + packet[3:]
                for b in packet:
                    os.write(master, bytes([b]))
                ack = read(1)
                assert ack == (b'\x33' if mode == 'checksum' else b'\xcc')

            try:
                assert read(2) == b'UU'
                os.write(master, b'\x00\xcc')
                offset = 0
                remaining = 0
                while True:
                    length = read(1)[0]
                    packet = read(length - 1)
                    assert sum(packet[1:]) & 255 == packet[0]
                    cmd, data = packet[1], packet[2:]
                    if mode == 'nack':
                        os.write(master, b'\x33')
                        break
                    os.write(master, b'\x00\xcc')
                    if cmd == 0x23:
                        reply(b'\x44' if mode == 'status' else b'\x40')
                        if mode in ('status', 'checksum'):
                            break
                    elif cmd == 0x2c:
                        assert mode != 'verify'
                        flash[:] = b'\xff' * SIZE
                    elif cmd == 0x21:
                        offset, remaining = struct.unpack('>II', data)
                        assert offset == 0 and remaining == SIZE
                    elif cmd == 0x24:
                        assert 0 < len(data) <= 252 and len(data) % 4 == 0
                        flash[offset:offset + len(data)] = data
                        offset += len(data)
                        remaining -= len(data)
                    elif cmd == 0x27:
                        assert struct.unpack('>III', data) == (0, SIZE, 0)
                        assert remaining == 0
                        assert flash == expected
                        crc = zlib.crc32(flash)
                        reply(struct.pack('>I', crc ^ (1 if mode == 'crc' else 0)))
                        if mode == 'crc':
                            break
                    elif cmd == 0x25:
                        break
                    else:
                        raise AssertionError(f'unexpected command {cmd:x}')
                code = proc.wait(timeout=5)
                assert (code == 0) == (mode in ('flash', 'verify')), (mode, code)
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait()
                os.close(master)
                os.close(slave)
                output.seek(0)
                log = output.read().decode()
            print(f'{mode}: passed')
            return log


if __name__ == '__main__':
    for case in ('flash', 'verify', 'nack', 'status', 'checksum', 'crc'):
        run(case)
