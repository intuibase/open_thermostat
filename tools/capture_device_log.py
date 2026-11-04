#!/usr/bin/env python3
"""Receive the thermostat's TCP log across reconnects without changing its config."""
import argparse
import select
import socket
import time
from pathlib import Path


def capture(bind, port, output, seconds=0, device=None):
    deadline = time.monotonic() + seconds if seconds else None
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((bind, port))
        listener.listen(4)
        print(f'Listening on {bind}:{port}; appending to {output}', flush=True)
        connection = None
        partial_line = False
        total = 0
        try:
            with open(output, 'a+b') as log:
                log.seek(0, 2)
                if log.tell():
                    log.seek(-1, 2)
                    if log.read(1) != b'\n':
                        log.write(b'\n')
                        log.flush()
                while deadline is None or time.monotonic() < deadline:
                    wait = min(1, max(0, deadline - time.monotonic())) if deadline else 1
                    watched = [listener] + ([connection] if connection is not None else [])
                    ready, _, _ = select.select(watched, [], [], wait)
                    if not ready:
                        continue
                    if listener in ready:
                        incoming, address = listener.accept()
                        if device and address[0] != device:
                            incoming.close()
                        else:
                            # A reboot need not FIN the old socket. Always accept the
                            # new connection instead of waiting on a half-open peer.
                            if connection is not None:
                                connection.close()
                                if partial_line:
                                    log.write(b'\n')
                                    log.flush()
                                    partial_line = False
                            connection = incoming
                            print(f'Connected: {address[0]}', flush=True)
                    if connection is None or connection not in ready:
                        continue
                    try:
                        chunk = connection.recv(65536)
                    except (ConnectionError, OSError):
                        chunk = b''
                    if not chunk:
                        connection.close()
                        connection = None
                        # Keep a truncated record separate from the next connection.
                        if partial_line:
                            log.write(b'\n')
                            log.flush()
                            partial_line = False
                        print('Disconnected; waiting for reconnect', flush=True)
                        continue
                    log.write(chunk)
                    log.flush()
                    partial_line = not chunk.endswith(b'\n')
                    total += len(chunk)
        finally:
            if connection is not None:
                connection.close()
        print(f'Captured {total} bytes', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='0.0.0.0', help='receiver address, not the thermostat address')
    parser.add_argument('--port', type=int, default=8701)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=0, help='0 = until Ctrl+C')
    parser.add_argument('--device', help='only accept this device IPv4 address')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or args.seconds < 0:
        parser.error('port must be 1..65535 and seconds nonnegative')
    try:
        capture(args.bind, args.port, args.output, args.seconds, args.device)
    except KeyboardInterrupt:
        pass
    except OSError as error:
        parser.exit(1, f'Capture failed: {error}\n')


if __name__ == '__main__':
    main()
