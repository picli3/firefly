import csv
import ctypes
import importlib.util
import itertools
import io
import os
from pathlib import Path
import shutil
import select
import struct
import subprocess
import tempfile
import threading
import sys
import unittest
from unittest.mock import patch
import zlib


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("csv_download", ROOT / "tools/csv_download.py")
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class Wire:
    def __init__(self, data):
        self.stream = io.BytesIO(data)
        self.commands = []

    def write(self, data):
        self.commands.append(data)

    def read(self, length):
        return self.stream.read(min(length, 7))

    def readline(self, limit):
        return self.stream.readline(limit)


def transfer(data, crc=None, size=None):
    size = len(data) if size is None else size
    crc = zlib.crc32(data) if crc is None else crc
    return (f"I (123) gateway: diagnostic\r\n@CSV BEGIN {size}\n".encode()
            + data + f"\n@CSV END {size} {crc:08x}\n".encode())


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "telemetry.csv"

    def test_fragmented_download_excludes_logs_and_framing(self):
        data = b"temperature_centi_c,flags\n-2450,31\n"
        wire = Wire(transfer(data))
        self.assertEqual(client.download(wire, self.output), (len(data), zlib.crc32(data)))
        self.assertEqual(self.output.read_bytes(), data)
        self.assertFalse(self.output.with_suffix(".csv.part").exists())
        self.assertEqual(wire.commands, [b"csv download\n"])

    def test_corruption_retains_partial_not_final(self):
        with self.assertRaises(client.TransferError):
            client.download(Wire(transfer(b"a,b\n1,2\n", crc=0)), self.output)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.output.with_suffix(".csv.part").read_bytes(), b"a,b\n1,2\n")

    def test_interrupted_payload(self):
        with self.assertRaisesRegex(client.TransferError, "interrupted"):
            client.download(Wire(b"@CSV BEGIN 100\na,b\n"), self.output)
        self.assertFalse(self.output.exists())

    def test_invalid_size_and_storage_error(self):
        for frame in (b"@CSV BEGIN 99999999\n", b"@CSV ERROR storage_unavailable\n"):
            with self.subTest(frame=frame), tempfile.TemporaryDirectory() as directory:
                with self.assertRaises(client.TransferError):
                    client.download(Wire(frame), Path(directory) / "out.csv")

    def test_existing_files_are_preserved(self):
        self.output.write_bytes(b"original")
        with self.assertRaises(client.TransferError):
            client.download(Wire(transfer(b"new\n")), self.output)
        self.assertEqual(self.output.read_bytes(), b"original")
        self.output.unlink()
        partial = self.output.with_suffix(".csv.part")
        partial.write_bytes(b"earlier failed download")
        with self.assertRaises(FileExistsError):
            client.download(Wire(transfer(b"new\n")), self.output)
        self.assertEqual(partial.read_bytes(), b"earlier failed download")

    def test_handshake_ignores_previous_abort(self):
        wire = Wire(b"@CSV ERROR aborted\n@CSV STATUS recording=1\n")
        self.assertEqual(client.response(wire, b"@CSV STATUS ", ignore_aborted=True),
                         b"@CSV STATUS recording=1")

    def test_wrong_trailer_size_is_rejected(self):
        data = b"a,b\n1,2\n"
        frame = transfer(data).replace(f"@CSV END {len(data)} ".encode(), b"@CSV END 1 ")
        with self.assertRaises(client.TransferError):
            client.download(Wire(frame), self.output)

    def test_handshake_retries_after_hardware_reset_on_open(self):
        class ResettingPort(Wire):
            timeout = 5

            def readline(self, limit):
                return b'booting\n' if len(self.commands) < 2 else b'@CSV STATUS recording=1\n'

        wire = ResettingPort(b'')
        ticks = itertools.count(0, 0.6)
        with patch.object(client.time, 'monotonic', side_effect=lambda: next(ticks)):
            self.assertEqual(client.synchronize(wire), b'@CSV STATUS recording=1')
        self.assertEqual(wire.timeout, 5)
        self.assertEqual(wire.commands, [b'\x03\ncsv status\n'] * 2)

    def test_handshake_timeout_restores_serial_timeout(self):
        wire = Wire(b'')
        wire.timeout = 5
        ticks = itertools.count()
        with patch.object(client.time, 'monotonic', side_effect=lambda: next(ticks)):
            with self.assertRaises(client.TransferError):
                client.synchronize(wire, timeout=3)
        self.assertEqual(wire.timeout, 5)


class RepeatStartupTests(unittest.TestCase):
    def test_status_retries_only_while_zigbee_is_starting(self):
        wire = Wire(b"@REPEAT ERROR ESP_ERR_INVALID_STATE\n@REPEAT STATUS node=00000000 phase=off remaining_s=0\n")
        with patch.object(client.time, "sleep"):
            self.assertIn("phase=off", client.wait_repeat_ready(wire))
        self.assertEqual(wire.commands, [b"zigbee repeat status\n"] * 2)

    def test_unexpected_error_is_not_retried(self):
        wire = Wire(b"@REPEAT ERROR ESP_ERR_TIMEOUT\n")
        with self.assertRaisesRegex(client.TransferError, "ESP_ERR_TIMEOUT"):
            client.wait_repeat_ready(wire)
        self.assertEqual(wire.commands, [b"zigbee repeat status\n"])


class SerialIntegrationTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "Pseudo-terminal test requires POSIX")
    def test_cli_download_over_real_pyserial_port(self):
        try:
            import serial  # noqa: F401
        except ImportError:
            self.skipTest("pyserial is not installed in this Python environment")
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        stopped = threading.Event()
        failures = []
        data = b"voltage_mv,temperature_centi_c,flags\n6120,-2450,31\n"

        def gateway():
            pending = b""
            try:
                while not stopped.is_set():
                    readable, _, _ = select.select([master], [], [], 0.1)
                    if not readable:
                        continue
                    pending += os.read(master, 512)
                    while b"\n" in pending:
                        command, pending = pending.split(b"\n", 1)
                        if command == b"csv status":
                            os.write(master, b"@CSV STATUS recording=1\n")
                        elif command == b"csv download":
                            wire = transfer(data)
                            for offset in range(0, len(wire), 11):
                                os.write(master, wire[offset:offset + 11])
            except BaseException as error:
                failures.append(error)

        thread = threading.Thread(target=gateway, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "download.csv"
                result = subprocess.run([sys.executable, str(ROOT / "tools/csv_download.py"),
                                         "--port", os.ttyname(slave), "download", str(output)],
                                        capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), data)
                self.assertIn(f"CRC32={zlib.crc32(data):08x}", result.stdout)
        finally:
            stopped.set()
            thread.join(timeout=2)
        self.assertFalse(failures, failures)


@unittest.skipUnless(shutil.which("cc"), "Host C compiler required")
class FirmwareFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "csv_format.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "main"), "-I", str(ROOT.parent / "common/include"),
                        str(ROOT / "main/csv_format.c"), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.csv_format_row.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32,
                                          ctypes.c_int64, ctypes.c_uint16, ctypes.c_void_p]
        cls.lib.csv_crc32_update.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.csv_crc32_update.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_firmware_row_units_flags_and_signed_fields(self):
        sample = struct.pack("<BBHIHhihBBI", 1, 31, 65535, 0x89abcdef,
                             6120, -1060, -6487, -2450, 78, 0, 95)
        output = ctypes.create_string_buffer(256)
        length = self.lib.csv_format_row(output, len(output), 42, 4294967296, 0x1234, sample)
        self.assertGreater(length, 0)
        header_address = ctypes.addressof((ctypes.c_char * 1).in_dll(self.lib, "CSV_HEADER"))
        header = ctypes.string_at(header_address).decode()
        row = next(csv.DictReader(io.StringIO(header + output.value.decode())))
        self.assertEqual(row, dict(zip(header.strip().split(","),
                         ["42", "4294967296", "0x1234", "89abcdef", "65535", "6120", "-1060",
                          "-6487", "-2450", "78", "31", "95"])))
        self.assertEqual(length, len(output.value))

    def test_rejects_truncated_row(self):
        output = ctypes.create_string_buffer(8)
        sample = bytes(24)
        self.assertEqual(self.lib.csv_format_row(output, len(output), 1, 1, 1, sample), -1)

    def test_firmware_crc_matches_python_across_chunks(self):
        state = 0xffffffff
        for data in (b"123", b"456", b"789"):
            state = self.lib.csv_crc32_update(state, data, len(data))
        self.assertEqual(state ^ 0xffffffff, 0xcbf43926)
        data = bytes(range(256)) * 8
        state = self.lib.csv_crc32_update(0xffffffff, data, len(data))
        self.assertEqual(state ^ 0xffffffff, zlib.crc32(data))


if __name__ == "__main__":
    unittest.main()
