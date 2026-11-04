"""TCP reconnect regression test; uses only loopback and temporary files."""
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


class CaptureTest(unittest.TestCase):
    def test_reconnect_replaces_half_open_connection(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'capture.log'
            with socket.socket() as reserve:
                reserve.bind(('127.0.0.1', 0))
                port = reserve.getsockname()[1]
            process = subprocess.Popen([
                sys.executable, 'tools/capture_device_log.py', '--bind', '127.0.0.1',
                '--port', str(port), '--output', str(log), '--seconds', '3',
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            clients = []
            try:
                deadline = time.monotonic() + 2
                while True:
                    try:
                        first = socket.create_connection(('127.0.0.1', port), timeout=0.2)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            self.fail('Receiver did not start')
                        time.sleep(0.02)
                clients.append(first)
                first.sendall('pierwszy Ł\n'.encode())
                while not log.exists() or b'\n' not in log.read_bytes():
                    if time.monotonic() >= deadline:
                        self.fail('First record not captured')
                    time.sleep(0.01)
                # Deliberately keep the old socket open, like a rebooted peer
                # whose TCP connection has not yet timed out at the receiver.
                second = socket.create_connection(('127.0.0.1', port), timeout=0.5)
                clients.append(second)
                second.sendall(b'next boot\n')
                while b'next boot\n' not in log.read_bytes():
                    if time.monotonic() >= deadline:
                        self.fail('Stale connection blocked reconnect')
                    time.sleep(0.01)
                second.close()
                code = process.wait(timeout=4)
                self.assertEqual(code, 0)
                self.assertEqual(log.read_text(), 'pierwszy Ł\nnext boot\n')
            finally:
                for client in clients:
                    client.close()
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=2)
                process.communicate()


if __name__ == '__main__':
    unittest.main()
