import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RemoteConfigHost(unittest.TestCase):
    def test_firmware_module_and_web_contract(self):
        idf = Path(os.environ.get("IDF_PATH", str(Path.home() / "esp/esp-idf")))
        cjson = idf / "components/json/cJSON"
        with tempfile.TemporaryDirectory(prefix="firefly-config-") as tmp:
            binary = str(Path(tmp) / "remote-config")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                "-I" + str(ROOT / "tools/tests/remote_config_stubs"),
                "-I" + str(ROOT / "main"), "-I" + str(ROOT.parent / "common/include"),
                "-I" + str(cjson), str(ROOT / "tools/tests/remote_config_host.c"),
                str(cjson / "cJSON.c"), "-lm", "-o", binary,
            ], check=True)
            result = subprocess.run([binary], check=True, capture_output=True, text=True)
            acks = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertGreaterEqual(len(acks), 8)
            web = ROOT.parent.parent / "firefly-web"
            if shutil.which("node") and (web / "node_modules/zod").exists():
                # Read-only validation through the same schema used by MQTT ingest.
                subprocess.run([
                    "node", "--input-type=module", "-e",
                    'import {ackSchema} from "./services/lib/domain.mjs";'
                    'let s=""; for await(const c of process.stdin) s+=c;'
                    'for(const line of s.trim().split("\\n")) ackSchema.parse(JSON.parse(line));',
                ], input=result.stdout, text=True, cwd=web, check=True)

    def test_ethernet_disabled_ack_stub(self):
        with tempfile.TemporaryDirectory(prefix="firefly-config-off-") as tmp:
            source = Path(tmp) / "stub.c"
            source.write_text('#include "remote_config.h"\nint main(void) {'
                              'remote_config_ack(0); return 0;}\n')
            binary = str(Path(tmp) / "stub")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DHOST_ETH_DISABLED",
                "-I" + str(ROOT / "tools/tests/remote_config_stubs"),
                "-I" + str(ROOT / "main"), "-I" + str(ROOT.parent / "common/include"),
                str(source), str(ROOT / "main/remote_config.c"), "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
