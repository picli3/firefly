import { readFile } from "node:fs/promises";
import { X509Certificate, createHash, randomUUID } from "node:crypto";
import { isIP } from "node:net";
import mqtt from "mqtt";

export const AT_CONTRACT = "firefly-at/v1-draft";
export function atBundle(config) {
  // A draft transport contract, not Espressif ESP-AT or the current C6 console.
  const raw = JSON.stringify(config);
  const encoded = Buffer.from(raw, "utf8").toString("base64");
  const chunks = encoded.match(/.{1,192}/g) || [];
  const checksum = createHash("sha256").update(raw, "utf8").digest("hex");
  return [
    "AT+FFPROVBEGIN=" + chunks.length,
    ...chunks.map((chunk, i) => `AT+FFPROVDATA=${i},"${chunk}"`),
    `AT+FFPROVCOMMIT="${checksum}"`,
    "AT+FFPROV?",
  ].join("\r\n");
}
export async function brokerInfo(env = process.env) {
  const warnings = [];
  const host = (env.MQTT_PUBLIC_HOST || env.MQTT_HOST || "localhost").trim();
  const port = Number(env.MQTT_PUBLIC_PORT || env.MQTT_PORT || 8883);
  const validHost =
    !!host &&
    host.length <= 253 &&
    (isIP(host) || /^[a-zA-Z0-9](?:[a-zA-Z0-9.-]*[a-zA-Z0-9])?$/.test(host));
  const validPort = Number.isInteger(port) && port >= 1 && port <= 65535;
  if (!validHost || !validPort)
    warnings.push(
      "Dirección o puerto del broker no válidos. Revise MQTT_PUBLIC_HOST y MQTT_PUBLIC_PORT.",
    );
  const loopback = /^(localhost|127\.|0\.0\.0\.0$|::1$|::$)/i.test(host);
  if (loopback)
    warnings.push(
      "Esta dirección es local al servidor. Configure un nombre o IP accesible desde la red del C6.",
    );
  const bind = env.MQTT_BIND || "127.0.0.1";
  if (/^(127\.|localhost$|::1$)/i.test(bind))
    warnings.push(
      "Mosquitto solo acepta conexiones del equipo local (MQTT_BIND). Todavía no es accesible desde un C6 de la red.",
    );
  let caPem = "",
    fingerprint = null,
    expiresAt = null,
    hostnameMatches = false,
    certificateValid = false;
  try {
    caPem = await readFile(
      env.BROKER_CA_FILE || "/broker-public/ca.pem",
      "utf8",
    );
    const cert = new X509Certificate(caPem);
    fingerprint = cert.fingerprint256;
    expiresAt = new Date(cert.validTo).toISOString();
    hostnameMatches = !!(isIP(host)
      ? cert.checkIP(host)
      : cert.checkHost(host));
    certificateValid =
      Date.now() >= Date.parse(cert.validFrom) &&
      Date.now() < Date.parse(cert.validTo);
    if (!hostnameMatches)
      warnings.push(
        "El certificado del broker no corresponde al nombre/IP anunciado. Configure el certificado antes de conectar el C6.",
      );
    if (!certificateValid)
      warnings.push(
        "El certificado del broker no está dentro de su periodo de validez.",
      );
  } catch {
    caPem = "";
    warnings.push(
      "No se pudo cargar el certificado público del broker. Revise el servicio certs y BROKER_CA_FILE.",
    );
  }
  const ntpServers = (env.GATEWAY_NTP_SERVERS || "pool.ntp.org")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  if (
    !ntpServers.length ||
    ntpServers.length > 3 ||
    ntpServers.some((s) => !/^[a-zA-Z0-9.:-]{1,253}$/.test(s))
  )
    throw Error("GATEWAY_NTP_SERVERS inválido");
  return {
    host,
    port,
    uri:
      validHost && validPort
        ? `mqtts://${isIP(host) === 6 ? "[" + host + "]" : host}:${port}`
        : null,
    tls: true,
    ca_pem: caPem,
    ca_fingerprint_sha256: fingerprint,
    ca_expires_at: expiresAt,
    certificate_matches_host: hostnameMatches,
    network_ready: !!(
      validHost &&
      validPort &&
      !loopback &&
      !/^(127\.|localhost$|::1$)/i.test(bind) &&
      hostnameMatches &&
      certificateValid
    ),
    warnings,
    ntp_servers: ntpServers,
    at_contract: AT_CONTRACT,
    at_supported_by_current_firmware: false,
  };
}
export function provisioningBundle(
  gateway,
  password,
  broker,
  credentialStatus,
) {
  const root = `firefly/v1/gateways/${gateway.id}`;
  const topics = {
    status: root + "/status",
    telemetry: root + "/telemetry",
    test_report: root + "/test-report",
    configuration_ack: root + "/config-ack",
    configuration_command: root + "/commands/config",
    individual_test_command: root + "/commands/test",
    individual_test_ack: root + "/test-ack",
  };
  const complete = !!(
    password &&
    broker.uri &&
    broker.ca_pem &&
    broker.certificate_matches_host
  );
  const config = complete
    ? {
        schema_version: 1,
        gateway_id: gateway.id,
        network: { interface: "ethernet", addressing: "dhcp" },
        time: { ntp_servers: broker.ntp_servers },
        mqtt: {
          uri: broker.uri,
          host: broker.host,
          port: broker.port,
          tls: true,
          verify_certificate: true,
          ca_pem: broker.ca_pem,
          client_id: "firefly-" + gateway.id,
          username: gateway.id,
          password,
          protocol_version: "3.1.1",
          qos: 1,
          keepalive_s: 60,
          heartbeat_interval_s: gateway.report_interval_s || 60,
          topic_prefix: root,
          topics,
        },
      }
    : null;
  return {
    gateway_id: gateway.id,
    gateway_name: gateway.name,
    broker,
    username: gateway.id,
    client_id: "firefly-" + gateway.id,
    password: password || null,
    password_available_once: !!password,
    topics,
    credential_status: credentialStatus,
    config,
    at_commands: config ? atBundle(config) : null,
    at_notice:
      "Contrato AT propuesto para el futuro firmware FireFly del C6. El firmware actual no reconoce estos comandos. No es Espressif ESP-AT.",
  };
}
export async function checkCredentials(username, password, env = process.env) {
  const deadline = Date.now() + 8000;
  do {
    const ok = await new Promise((resolve) => {
      const client = mqtt.connect(env.MQTT_URL || "mqtt://mosquitto:1883", {
        username,
        password,
        clientId: "provision-check-" + randomUUID(),
        reconnectPeriod: 0,
        connectTimeout: 1200,
        clean: true,
      });
      let finished = false;
      const finish = (result) => {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        client.end(true);
        resolve(result);
      };
      const timer = setTimeout(() => finish(false), 1500);
      client.once("connect", () => finish(true));
      client.once("error", () => finish(false));
      client.once("close", () => finish(false));
    }).catch(() => false);
    if (ok) return "verified";
    await new Promise((resolve) => setTimeout(resolve, 400));
  } while (Date.now() < deadline);
  return "pending";
}
