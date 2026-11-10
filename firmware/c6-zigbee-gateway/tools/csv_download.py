#!/usr/bin/env python3
"""Download a verified telemetry CSV over the gateway's serial console."""

import argparse
import os
from pathlib import Path
import re
import sys
import time
import zlib


class TransferError(Exception):
    pass


def response(port, prefix, timeout=15, ignore_aborted=False):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline(512).strip()
        if ignore_aborted and line == b"@CSV ERROR aborted":
            continue
        if line.startswith(b"@CSV ERROR"):
            raise TransferError(line.decode("ascii", errors="replace"))
        if line.startswith(prefix):
            return line
    raise TransferError("No response from gateway (check port and close idf.py monitor)")


def synchronize(port, timeout=15):
    """Opening a serial port can reset the board even with DTR/RTS low.

    Retry only the idempotent status command until firmware has finished booting.
    Never retry download/clear blindly: those require an established session.
    """
    original_timeout = port.timeout
    port.timeout = 0.25
    deadline = time.monotonic() + timeout
    next_request = 0
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_request:
                port.write(b"\x03\ncsv status\n")
                next_request = now + 1
            line = port.readline(512).strip()
            if line.startswith(b"@CSV STATUS "):
                return line
        raise TransferError("Gateway did not become ready (check port and close idf.py monitor)")
    finally:
        port.timeout = original_timeout


def wait_repeat_ready(port, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        port.write(b"zigbee repeat status\n")
        result = response(port, b"@REPEAT ", timeout=max(0, deadline - time.monotonic())).decode("ascii")
        if result != "@REPEAT ERROR ESP_ERR_INVALID_STATE":
            if result.startswith("@REPEAT STATUS "):
                return result
            raise TransferError(result)
        time.sleep(0.25)
    raise TransferError("Zigbee did not become ready")


def download(port, destination):
    destination = Path(destination)
    partial = destination.with_name(destination.name + ".part")
    if destination.exists():
        raise TransferError(f"Output already exists: {destination}")
    # Exclusive creation preserves earlier failed transfers too.
    with partial.open("xb") as output:
        port.write(b"csv download\n")
        begin = response(port, b"@CSV BEGIN ")
        match = re.fullmatch(rb"@CSV BEGIN ([0-9]+)", begin)
        if not match:
            raise TransferError("Invalid transfer header")
        size = int(match[1])
        if not 0 < size <= 2 * 1024 * 1024:
            raise TransferError("Invalid snapshot size")
        received = 0
        crc = 0
        while received < size:
            chunk = port.read(min(4096, size - received))
            if not chunk:
                raise TransferError(f"Transfer interrupted: {received}/{size} bytes; retained {partial}")
            output.write(chunk)
            crc = zlib.crc32(chunk, crc)
            received += len(chunk)
        end = response(port, b"@CSV END ")
        match = re.fullmatch(rb"@CSV END ([0-9]+) ([0-9a-fA-F]{8})", end)
        if not match or int(match[1]) != size or int(match[2], 16) != crc:
            raise TransferError(f"Size/CRC mismatch; retained {partial}")
        output.flush()
        os.fsync(output.fileno())
    # Linking rather than replacing never overwrites a destination created meanwhile.
    os.link(partial, destination)
    partial.unlink()
    return size, crc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Native USB port, e.g. /dev/ttyACM0 or COM3")
    sub = parser.add_subparsers(dest="command", required=True)
    get = sub.add_parser("download")
    get.add_argument("output", type=Path)
    sub.add_parser("status")
    repeat_start = sub.add_parser("repeat-start", help="Start 30 s tests with 5 min rests for one lamp")
    repeat_start.add_argument("device_id", help="Eight hexadecimal digits")
    sub.add_parser("repeat-status", help="Show the repeating test state")
    sub.add_parser("repeat-stop", help="Stop sending future tests; a running test ends on its own")
    clear = sub.add_parser("clear")
    clear.add_argument("--confirm", action="store_true", help="Explicitly authorize deleting the gateway CSV")
    args = parser.parse_args()
    if args.command == "clear" and not args.confirm:
        parser.error("clear requires --confirm; download your CSV first")
    if args.command == "repeat-start" and not re.fullmatch(r"[0-9a-fA-F]{8}", args.device_id):
        parser.error("device_id must be eight hexadecimal digits")
    try:
        import serial
    except ImportError:
        parser.error("Install dependencies: python -m pip install -r tools/requirements.txt")
    try:
        # Set control lines before opening to avoid the usual ESP32 auto-reset.
        port = serial.Serial(baudrate=115200, timeout=5, write_timeout=5)
        port.dtr = False
        port.rts = False
        port.port = args.port
        with port:
            port.reset_input_buffer()
            initial_status = synchronize(port)
            try:
                if args.command == "download":
                    size, crc = download(port, args.output)
                    print(f"Saved {args.output}: {size} bytes, CRC32={crc:08x}")
                elif args.command == "status":
                    print(initial_status.decode("ascii"))
                elif args.command.startswith("repeat-"):
                    operation = args.command.removeprefix("repeat-")
                    result = wait_repeat_ready(port)
                    if operation != "status":
                        if operation == "start":
                            operation += f" {args.device_id}"
                        port.write(f"zigbee repeat {operation}\n".encode("ascii"))
                        result = response(port, b"@REPEAT ").decode("ascii")
                        if result.startswith("@REPEAT ERROR"):
                            raise TransferError(result)
                    print(result)
                else:
                    port.write(b"csv clear CONFIRM\n")
                    print(response(port, b"@CSV OK ").decode("ascii"))
            except (OSError, TransferError, serial.SerialException, KeyboardInterrupt):
                port.write(b"\x03\n")
                raise
    except (OSError, TransferError, serial.SerialException, KeyboardInterrupt) as error:
        print(f"CSV operation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
