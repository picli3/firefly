from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BatteryConfigHost(unittest.TestCase):
    def test_real_state_machine_with_storage_faults(self):
        self.compile_run("battery_config_host.c")

    def test_test_report_uses_start_revision(self):
        self.compile_run("battery_test_host.c")

    def test_measurements_use_configured_thresholds(self):
        self.compile_run("telemetry_config_host.c")

    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix="firefly-h2-config-") as tmp:
            binary = str(Path(tmp) / "config")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-variable",
                "-Wno-unused-parameter", "-I" + str(ROOT / "tools/tests/stubs"),
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + str(ROOT / "main"), "-I" + str(ROOT.parent / "common/include"),
                str(ROOT / "tools/tests" / source), "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
