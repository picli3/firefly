import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import {
  brokerInfo,
  atBundle,
  provisioningBundle,
  AT_CONTRACT,
} from "../services/lib/gateway-provisioning.mjs";

test("contrato AT propuesto: fragmentos ordenados, escapado seguro e integridad", () => {
  const config = {
    schema_version: 1,
    mqtt: {
      password: 'password-"\\=\r\nAT+malicious',
      ca_pem: "-----CERT-----\nABC\nEND",
    },
  };
  const lines = atBundle(config).split("\r\n");
  assert.match(lines[0], /^AT\+FFPROVBEGIN=\d+$/);
  const chunks = lines.slice(1, -2).map((line, i) => {
    const match = /^AT\+FFPROVDATA=(\d+),"([A-Za-z0-9+/=]+)"$/.exec(line);
    assert.ok(match);
    assert.equal(Number(match[1]), i);
    assert.ok(line.length <= 220);
    return match[2];
  });
  assert.equal(Number(lines[0].split("=")[1]), chunks.length);
  const raw = Buffer.from(chunks.join(""), "base64").toString("utf8");
  assert.deepEqual(JSON.parse(raw), config);
  assert.equal(
    lines.at(-2),
    `AT+FFPROVCOMMIT="${createHash("sha256").update(raw).digest("hex")}"`,
  );
  assert.equal(lines.at(-1), "AT+FFPROV?");
});
test("sin contraseña no se reconstruyen secretos ni comandos", () => {
  const bundle = provisioningBundle(
    { id: "gateway-01", name: "A" },
    null,
    {
      host: "localhost",
      uri: "mqtts://localhost:8883",
      port: 8883,
      ca_pem: "CA",
      certificate_matches_host: true,
    },
    "unknown",
  );
  assert.equal(bundle.password, null);
  assert.equal(bundle.config, null);
  assert.equal(bundle.at_commands, null);
  assert.equal(bundle.username, "gateway-01");
  assert.match(bundle.topics.telemetry, /gateway-01\/telemetry$/);
});
test("dirección anunciada y certificado real: loopback, DNS, IP y errores visibles", async () => {
  const folder = await mkdtemp(join(tmpdir(), "firefly-provision-"));
  try {
    const cert = join(folder, "ca.pem");
    execFileSync(
      "openssl",
      [
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-nodes",
        "-keyout",
        join(folder, "key.pem"),
        "-out",
        cert,
        "-days",
        "1",
        "-subj",
        "/CN=broker.firefly.test",
        "-addext",
        "subjectAltName=DNS:broker.firefly.test,IP:192.0.2.40",
      ],
      { stdio: "ignore" },
    );
    const base = {
      MQTT_PUBLIC_HOST: "broker.firefly.test",
      MQTT_PUBLIC_PORT: "8883",
      MQTT_BIND: "0.0.0.0",
      BROKER_CA_FILE: cert,
    };
    const info = await brokerInfo(base);
    assert.equal(info.network_ready, true);
    assert.equal(info.uri, "mqtts://broker.firefly.test:8883");
    assert.equal(info.at_supported_by_current_firmware, false);
    assert.equal(info.at_contract, AT_CONTRACT);
    assert.ok(info.ca_fingerprint_sha256);
    const ip = await brokerInfo({ ...base, MQTT_PUBLIC_HOST: "192.0.2.40" });
    assert.equal(ip.certificate_matches_host, true);
    const local = await brokerInfo({
      ...base,
      MQTT_PUBLIC_HOST: "localhost",
      MQTT_BIND: "127.0.0.1",
    });
    assert.equal(local.network_ready, false);
    assert.ok(local.warnings.some((s) => s.includes("equipo local")));
    const mismatch = await brokerInfo({
      ...base,
      MQTT_PUBLIC_HOST: "wrong.test",
    });
    assert.equal(mismatch.certificate_matches_host, false);
    assert.equal(
      provisioningBundle({ id: "g" }, "some-password", mismatch, "pending")
        .config,
      null,
    );
    const missing = await brokerInfo({
      ...base,
      BROKER_CA_FILE: join(folder, "missing"),
    });
    assert.equal(missing.ca_pem, "");
    assert.ok(missing.warnings.some((s) => s.includes("certificado")));
    const malformed = await brokerInfo({
      ...base,
      MQTT_PUBLIC_HOST: "mqtt://wrong",
      MQTT_PUBLIC_PORT: "99999",
    });
    assert.equal(malformed.uri, null);
    assert.equal(malformed.network_ready, false);
  } finally {
    await rm(folder, { recursive: true, force: true });
  }
});
