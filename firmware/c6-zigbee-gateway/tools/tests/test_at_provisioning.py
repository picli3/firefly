import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProvisioningHost(unittest.TestCase):
    def test_production_parser_and_atomic_store(self):
        idf = Path(os.environ.get("IDF_PATH", str(Path.home() / "esp/esp-idf")))
        cjson = idf / "components/json/cJSON"
        with tempfile.TemporaryDirectory(prefix="firefly-at-") as tmp:
            tmp = Path(tmp)
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                            "-subj", "/CN=host-test", "-days", "1", "-keyout", str(tmp / "key.pem"),
                            "-out", str(tmp / "ca.pem")], check=True, capture_output=True)
            root = "firefly/v1/gateways/unit-gateway"
            config = {"schema_version": 1, "gateway_id": "unit-gateway",
                      "network": {"interface": "ethernet", "addressing": "dhcp"},
                      "time": {"ntp_servers": ["pool.ntp.org", "time.cloudflare.com", "time.google.com"]},
                      "mqtt": {"uri": "mqtts://192.168.18.40:8883", "host": "192.168.18.40", "port": 8883,
                               "tls": True, "verify_certificate": True, "ca_pem": (tmp / "ca.pem").read_text(),
                               "client_id": "unit-client", "username": "unit-gateway", "password": "unit-secret",
                               "protocol_version": "3.1.1", "qos": 1, "keepalive_s": 60,
                               "heartbeat_interval_s": 60, "topic_prefix": root,
                               "topics": {k: root + "/" + v for k, v in {
                                   "status": "status", "telemetry": "telemetry", "test_report": "test-report",
                                   "configuration_ack": "config-ack", "configuration_command": "commands/config",
                                   "individual_test_command": "commands/test", "individual_test_ack": "test-ack"}.items()}}}
            (tmp / "fixture.json").write_text(json.dumps(config, separators=(",", ":")))
            binary = str(tmp / "host")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-g",
                            "-I" + str(ROOT / "tools/tests/at_stubs"), "-I" + str(ROOT / "main"),
                            "-I" + str(ROOT.parent / "common/include"), "-I" + str(cjson),
                            str(ROOT / "tools/tests/at_provisioning_host.c"), str(cjson / "cJSON.c"),
                            "-lm", "-lcrypto", "-o", binary], check=True)
            subprocess.run([binary, str(tmp / "fixture.json")], check=True)

    def test_uploader_matches_web_chunk_contract(self):
        spec = importlib.util.spec_from_file_location("gateway_at", ROOT / "tools/gateway_at.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        import base64
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bundle.json"
            config = {"schema_version": 1, "value": "x" * 1000}
            path.write_text(json.dumps({"at_supported_by_current_firmware": False, "config": config}))
            lines = module.encode_bundle(path)
            encoded = "".join(line.split('"')[1] for line in lines[1:-1])
            raw = base64.b64decode(encoded)
            self.assertEqual(json.loads(raw), config)
            self.assertIn(hashlib.sha256(raw).hexdigest(), lines[-1])
            self.assertTrue(all(len(line) <= 220 for line in lines))


if __name__ == "__main__":
    unittest.main()
