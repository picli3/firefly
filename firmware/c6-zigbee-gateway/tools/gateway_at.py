#!/usr/bin/env python3
"""Provision FireFly C6 over native USB without exposing credential payloads."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import sys
import time


class ATError(Exception):
    pass


def encode_bundle(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ATError("Expected a JSON object")
    config = data.get("config", data)
    if not isinstance(config, dict) or config.get("schema_version") != 1:
        raise ATError("Expected a FireFly schema_version 1 configuration")
    raw = json.dumps(config, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if not 0 < len(raw) <= 8192:
        raise ATError("Configuration exceeds the 8192-byte firmware limit")
    encoded = base64.b64encode(raw).decode("ascii")
    chunks = [encoded[i:i + 192] for i in range(0, len(encoded), 192)]
    lines = [f"AT+FFPROVBEGIN={len(chunks)}"]
    lines += [f'AT+FFPROVDATA={i},"{chunk}"' for i, chunk in enumerate(chunks)]
    lines += [f'AT+FFPROVCOMMIT="{hashlib.sha256(raw).hexdigest()}"']
    return lines


def command(port, text, timeout=15):
    """Firmware never echoes input. Ignore application logs; never print payload."""
    port.write(text.encode("ascii") + b"\r\n")
    deadline = time.monotonic() + timeout
    result = []
    while time.monotonic() < deadline:
        line = port.readline(2048).strip()
        if line == b"OK":
            return result
        if line.startswith(b"ERROR:"):
            raise ATError(line.decode("ascii", errors="replace"))
        if line.startswith((b"+FFAT:", b"+FFPROV:", b"+FFSTATUS:")):
            result.append(line.decode("ascii", errors="replace"))
    raise ATError("No AT response; verify firmware, USB port and closed serial monitors")


def wait_ready(port, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            reply = command(port, "ATI", timeout=2)
            if any(line.startswith("+FFAT: firefly-at/v1") for line in reply):
                return
            raise ATError("Unsupported AT implementation")
        except ATError:
            time.sleep(.25)  # Only retry the read-only handshake.
    raise ATError("Gateway did not start a compatible FireFly AT console")


def wait_mqtt(port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = command(port, "AT+FFSTATUS?", timeout=3)
        if any("mqtt_connected=1" in line for line in result):
            return result
        time.sleep(.5)
    raise ATError("Settings remain saved, but MQTT did not connect; check Ethernet, broker, TLS and credentials")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--wait-mqtt", type=int, default=0, metavar="SECONDS",
                        help="Wait up to 60 seconds for a real MQTT connection")
    sub = parser.add_subparsers(dest="operation", required=True)
    sub.add_parser("info")
    sub.add_parser("restart")
    load = sub.add_parser("load")
    load.add_argument("json_file", type=Path)
    load.add_argument("--restart", action="store_true", help="Apply saved configuration by restarting C6")
    args = parser.parse_args()
    if not 0 <= args.wait_mqtt <= 60:
        parser.error("--wait-mqtt must be between 0 and 60")
    try:
        import serial
        lines = encode_bundle(args.json_file) if args.operation == "load" else []
        port = serial.Serial(baudrate=115200, timeout=.25, write_timeout=5, exclusive=True)
        port.dtr = False
        port.rts = False
        port.port = args.port
        with port:
            port.reset_input_buffer()
            wait_ready(port)
            if args.operation == "load":
                # No blind COMMIT retry after ambiguous transport failure.
                for line in lines:
                    command(port, line)
                print("Configuration validated and saved atomically; credentials are not displayed.")
            restart = args.operation == "restart" or (args.operation == "load" and args.restart)
            if restart:
                command(port, "AT+RST")
                time.sleep(1)
                wait_ready(port)
                print("Gateway restarted; saved settings are active.")
            for query in ("AT+FFPROV?", "AT+FFSTATUS?"):
                for line in command(port, query):
                    print(line)
            if args.wait_mqtt:
                for line in wait_mqtt(port, args.wait_mqtt):
                    print(line)
        return 0
    except ImportError:
        print("Install pyserial: python -m pip install -r tools/requirements.txt", file=sys.stderr)
    except (ATError, OSError, ValueError, TypeError, KeyboardInterrupt) as exc:
        # Neither request text/base64 nor configuration values are included.
        print(f"AT operation failed ({type(exc).__name__}): {exc}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
