import test from "node:test";
import assert from "node:assert/strict";
import mqtt from "mqtt";
import { readFile } from "node:fs/promises";
import { randomUUID } from "node:crypto";
test(
  "Mosquitto TLS: autenticación y aislamiento real entre gateways",
  { skip: process.env.MQTT_TEST !== "1" },
  async () => {
    const demo = JSON.parse(await readFile(".runtime/demo.json", "utf8")),
      ca = await readFile(".runtime/mqtt-ca.crt");
    const bad = mqtt.connect("mqtts://localhost:8883", {
      username: demo.gateway,
      password: "incorrect-password",
      ca,
      reconnectPeriod: 0,
      connectTimeout: 3000,
      protocolVersion: 5,
    });
    const rejected = await new Promise((resolve) => {
      bad.on("error", () => resolve(true));
      bad.on("connect", () => resolve(false));
    });
    assert.equal(rejected, true);
    bad.end(true);
    const client = await mqtt.connectAsync("mqtts://localhost:8883", {
      username: demo.gateway,
      password: demo.password,
      ca,
      reconnectPeriod: 0,
      connectTimeout: 5000,
      protocolVersion: 5,
    });
    async function result(topic) {
      return new Promise((resolve) =>
        client.publish(topic, "{}", { qos: 1 }, (err, packet) =>
          resolve({ err, packet }),
        ),
      );
    }
    const denied = await result(
      "firefly/v1/gateways/another-gateway/telemetry",
    );
    assert.ok(
      denied.err || denied.packet?.reasonCode >= 128,
      "Publicación ajena debe rechazarse",
    );
    const command = await result(
      `firefly/v1/gateways/${demo.gateway}/commands/config`,
    );
    assert.ok(
      command.err || command.packet?.reasonCode >= 128,
      "Gateway no publica órdenes",
    );
    const own = await result(`firefly/v1/gateways/${demo.gateway}/status`);
    assert.equal(!!own.err, false);
    await client.endAsync();
  },
);
