import test from "node:test";
import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";
import mqtt from "mqtt";
test(
  "ACL del broker se actualiza sin rotar credenciales ni reiniciar Mosquitto",
  { skip: process.env.ACL_RELOAD_INTEGRATION !== "1" },
  async () => {
    const { db } = await import("../services/lib/db.mjs");
    const source = process.env.ACL_TEST_SOURCE;
    assert.ok(source?.endsWith("/.runtime/acl-reload-fixture.acl"));
    const policy = await readFile(source, "utf8");
    assert.ok(!policy.includes("/commands/test"));
    const origin = process.env.PUBLIC_ORIGIN,
      tag = "acl-" + randomUUID().slice(0, 8),
      root = `firefly/v1/gateways/${tag}`;
    let cookie = "",
      gateway,
      client,
      worker;
    async function req(path, body) {
      const r = await fetch(origin + "/api/" + path, {
        method: "POST",
        headers: {
          Origin: origin,
          "Content-Type": "application/json",
          ...(cookie ? { Cookie: cookie } : {}),
        },
        body: JSON.stringify(body),
      });
      assert.equal(r.status, 200, await r.clone().text());
      if (path === "auth/login")
        cookie = r.headers.get("set-cookie").split(";")[0];
      return r.json();
    }
    try {
      await req("auth/login", {
        email: process.env.ADMIN_EMAIL,
        password: process.env.ADMIN_PASSWORD,
      });
      gateway = await req("gateways", { id: tag, name: tag });
      client = await mqtt.connectAsync(process.env.MQTT_URL, {
        username: tag,
        password: gateway.password,
        reconnectPeriod: 0,
        protocolVersion: 5,
      });
      worker = await mqtt.connectAsync(process.env.MQTT_URL, {
        username: "firefly-worker",
        password: process.env.MQTT_PASSWORD,
        reconnectPeriod: 0,
      });
      const [before] = await db`SELECT mqtt_hash FROM gateways WHERE id=${tag}`;
      const ack = {
        schema_version: 1,
        message_id: randomUUID(),
        command_id: randomUUID(),
        device_id: "abcdef01",
        test_id: 65535,
        status: "rejected",
        reason: "Prueba aislada de permisos",
      };
      async function allowed() {
        try {
          await client.publishAsync(
            root + "/test-ack",
            JSON.stringify({ ...ack, message_id: randomUUID() }),
            { qos: 1 },
          );
          return true;
        } catch {
          return false;
        }
      }
      assert.equal(
        await allowed(),
        false,
        "La política inicial no autoriza el topic nuevo",
      );
      await writeFile(
        source,
        policy +
          "pattern read firefly/v1/gateways/%u/commands/test\npattern write firefly/v1/gateways/%u/test-ack\n",
      );
      const deadline = Date.now() + 10000;
      let changed = false;
      while (Date.now() < deadline) {
        if (await allowed()) {
          changed = true;
          break;
        }
        await new Promise((r) => setTimeout(r, 200));
      }
      assert.equal(
        changed,
        true,
        "El worker debe actualizar ACL aunque no cambien las contraseñas",
      );
      const [after] = await db`SELECT mqtt_hash FROM gateways WHERE id=${tag}`;
      assert.equal(after.mqtt_hash, before.mqtt_hash);
      await client.subscribeAsync(root + "/commands/test", { qos: 1 });
      const payload = randomUUID();
      const received = new Promise((resolve, reject) => {
        const timer = setTimeout(
          () => reject(Error("No llegó el mensaje autorizado")),
          3000,
        );
        client.on("message", (_t, b) => {
          if (b.toString() === payload) {
            clearTimeout(timer);
            resolve(true);
          }
        });
      });
      await worker.publishAsync(root + "/commands/test", payload, {
        qos: 1,
        retain: false,
      });
      assert.equal(await received, true);
      await assert.rejects(() =>
        client.publishAsync(
          "firefly/v1/gateways/another-gateway/test-ack",
          "{}",
          { qos: 1 },
        ),
      );
    } finally {
      if (client) await client.endAsync();
      if (worker) await worker.endAsync();
      if (gateway) {
        await db`DELETE FROM messages WHERE gateway_id=${tag}`;
        await db`DELETE FROM gateways WHERE id=${tag}`;
        await db`DELETE FROM audits WHERE target=${tag}`;
      }
      await db.end();
    }
  },
);
