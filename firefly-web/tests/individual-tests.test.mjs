import test from "node:test";
import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import mqtt from "mqtt";
test(
  "prueba individual: API → MQTT → confirmación → resultado, sin repetir órdenes",
  { skip: process.env.INDIVIDUAL_TEST_INTEGRATION !== "1" },
  async () => {
    const { db } = await import("../services/lib/db.mjs");
    const { expireTests, dispatchTests } =
      await import("../services/lib/individual-tests.mjs");
    const origin = process.env.PUBLIC_ORIGIN,
      tag = "individual-" + randomUUID().slice(0, 8);
    let cookie = "",
      client,
      gw,
      lamp,
      monitor;
    let sent = [],
      sequence = 0;
    async function req(path, method = "GET", body, auth = cookie) {
      const r = await fetch(origin + "/api/" + path, {
        method,
        headers: {
          Origin: origin,
          ...(auth ? { Cookie: auth } : {}),
          ...(body ? { "Content-Type": "application/json" } : {}),
        },
        body: body ? JSON.stringify(body) : undefined,
      });
      const text = await r.text();
      return {
        status: r.status,
        body: r.headers.get("content-type")?.includes("json")
          ? JSON.parse(text)
          : text,
        cookie: r.headers.get("set-cookie")?.split(";")[0],
      };
    }
    async function ok(path, method = "GET", body) {
      const r = await req(path, method, body);
      assert.equal(r.status, 200, JSON.stringify(r.body));
      return r.body;
    }
    async function until(fn) {
      const deadline = Date.now() + 12000;
      while (Date.now() < deadline) {
        const r = await fn();
        if (r) return r;
        await new Promise((r) => setTimeout(r, 100));
      }
      assert.fail("Tiempo de espera agotado");
    }
    async function publish(kind, data) {
      await client.publishAsync(
        `firefly/v1/gateways/${tag}/${kind}`,
        JSON.stringify({
          schema_version: 1,
          message_id: randomUUID(),
          ...data,
        }),
        { qos: 1, retain: false },
      );
    }
    async function sample(flags = 9) {
      await publish("telemetry", {
        device_id: "abcdef01",
        boot_id: tag,
        live: true,
        sequence: ++sequence,
        uptime_s: sequence * 5,
        report_interval_s: 60,
        voltage_mv: 6000,
        current_ma: 20,
        power_mw: 120,
        temperature_centi_c: 2500,
        soh_pct: 90,
        flags,
        config_capable: false,
      });
      await until(async () => {
        const l = (await ok("lamps/" + lamp.id)).lamp;
        return l.latest?.sequence === sequence;
      });
    }
    async function state(id) {
      return (await ok("lamps/" + lamp.id)).test_requests.find(
        (r) => r.id === id,
      );
    }
    const result = (cmd, id = cmd.command_id) => ({
      command_id: id,
      device_id: "abcdef01",
      boot_id: tag,
      live: true,
      test_id: cmd.test_id,
      result: "LOW_VOLTAGE",
      voltage_before_mv: 6000,
      voltage_min_mv: 5000,
      current_avg_ma: 350,
      current_peak_ma: 400,
      power_avg_mw: 1800,
      temperature_max_centi_c: 2600,
      duration_s: 30,
      sample_count: 15,
      flags: 8,
    });
    try {
      const login = await req("auth/login", "POST", {
        email: process.env.ADMIN_EMAIL,
        password: process.env.ADMIN_PASSWORD,
      });
      assert.equal(login.status, 200);
      cookie = login.cookie;
      gw = await ok("gateways", "POST", { id: tag, name: tag });
      lamp = await ok("lamps", "POST", {
        gateway_id: tag,
        device_id: "abcdef01",
        name: tag,
      });
      monitor = await ok("users", "POST", {
        email: tag + "@example.invalid",
        password: "testMonitorPassword42",
        name: tag,
        role: "monitor",
      });
      const m = await req("auth/login", "POST", {
        email: tag + "@example.invalid",
        password: "testMonitorPassword42",
      });
      assert.equal(
        (
          await req(
            "lamps/" + lamp.id + "/test",
            "POST",
            { request_id: randomUUID() },
            m.cookie,
          )
        ).status,
        403,
      );
      client = await mqtt.connectAsync(process.env.MQTT_URL, {
        username: tag,
        password: gw.password,
        reconnectPeriod: 0,
      });
      await client.subscribeAsync(`firefly/v1/gateways/${tag}/commands/test`, {
        qos: 1,
      });
      client.on("message", (_t, raw, packet) => {
        const cmd = JSON.parse(raw);
        assert.equal(packet.retain, false);
        sent.push(cmd);
      });
      await publish("status", {
        online: true,
        boot_id: tag,
        test_capable: false,
      });
      await sample();
      assert.equal(
        (
          await req("lamps/" + lamp.id + "/test", "POST", {
            request_id: randomUUID(),
          })
        ).status,
        409,
      );
      await publish("status", {
        online: true,
        boot_id: tag,
        test_capable: true,
      });
      await until(
        async () => (await ok("lamps/" + lamp.id)).lamp.gateway_test_capable,
      );
      await sample(8);
      assert.equal(
        (
          await req("lamps/" + lamp.id + "/test", "POST", {
            request_id: randomUUID(),
          })
        ).status,
        409,
      );
      await sample();
      const id = randomUUID();
      const first = await ok("lamps/" + lamp.id + "/test", "POST", {
        request_id: id,
      });
      const again = await ok("lamps/" + lamp.id + "/test", "POST", {
        request_id: id,
      });
      assert.equal(again.id, first.id);
      assert.equal(
        (
          await req("lamps/" + lamp.id + "/test", "POST", {
            request_id: randomUUID(),
          })
        ).status,
        409,
      );
      await until(() => sent.length === 1);
      const cmd = sent[0];
      assert.equal(cmd.command_id, id);
      assert.equal(cmd.duration_s, 30);
      assert.ok(cmd.test_id >= 32768);
      assert.ok(cmd.expires_at_ms > cmd.issued_at_ms);
      assert.equal(
        (await req("lamps/" + lamp.id + "/trash", "POST")).status,
        409,
      );
      await publish("test-ack", {
        command_id: id,
        device_id: "other",
        test_id: cmd.test_id,
        status: "accepted",
      });
      await new Promise((r) => setTimeout(r, 100));
      assert.equal((await state(id)).status, "sent");
      await publish("test-ack", {
        command_id: id,
        device_id: "abcdef01",
        test_id: cmd.test_id,
        status: "accepted",
      });
      await until(async () => (await state(id)).status === "accepted");
      await publish("test-report", result(cmd, randomUUID()));
      await new Promise((r) => setTimeout(r, 100));
      assert.equal((await state(id)).status, "accepted");
      await publish("test-report", result(cmd));
      await until(async () => (await state(id)).status === "completed");
      assert.equal((await state(id)).result.result, "LOW_VOLTAGE");
      await publish("test-ack", {
        command_id: id,
        device_id: "abcdef01",
        test_id: cmd.test_id,
        status: "accepted",
      });
      assert.equal(
        (await ok("lamps/" + lamp.id + "/test", "POST", { request_id: id })).id,
        id,
      );
      assert.equal(sent.length, 1);
      const second = await ok("lamps/" + lamp.id + "/test", "POST", {
        request_id: randomUUID(),
      });
      assert.notEqual(second.test_id, first.test_id);
      await until(() => sent.length === 2);
      await db`UPDATE test_requests SET deadline_at=now()-interval '1 second' WHERE id=${second.id}`;
      await expireTests();
      assert.equal((await state(second.id)).status, "unknown");
      let repeated = 0;
      await dispatchTests({
        publishAsync: async () => {
          repeated++;
        },
      });
      assert.equal(repeated, 0);
      await publish("test-ack", {
        command_id: second.id,
        device_id: "abcdef01",
        test_id: second.test_id,
        status: "accepted",
      });
      await new Promise((r) => setTimeout(r, 100));
      assert.equal((await state(second.id)).status, "unknown");
      await publish("test-report", result(sent[1]));
      await until(async () => (await state(second.id)).status === "completed");
      const expired = await ok("lamps/" + lamp.id + "/test", "POST", {
        request_id: randomUUID(),
      });
      await db`UPDATE test_requests SET expires_at=now()-interval '1 second' WHERE id=${expired.id} AND status='pending'`;
      await expireTests();
      assert.equal((await state(expired.id)).status, "expired");
      assert.equal(sent.length, 2);
    } finally {
      if (client) await client.endAsync();
      await db.begin(async (sql) => {
        if (lamp) {
          await sql`DELETE FROM test_requests WHERE lamp_id=${lamp.id}`;
          for (const table of [
            "measurements",
            "test_reports",
            "commands",
            "retired_boots",
          ])
            await sql`DELETE FROM ${sql(table)} WHERE lamp_id=${lamp.id}`;
          await sql`DELETE FROM lamps WHERE id=${lamp.id}`;
          await sql`DELETE FROM audits WHERE target=${lamp.id}`;
        }
        if (gw) {
          await sql`DELETE FROM messages WHERE gateway_id=${tag}`;
          await sql`DELETE FROM gateways WHERE id=${tag}`;
          await sql`DELETE FROM audits WHERE target=${tag}`;
        }
        if (monitor) {
          await sql`DELETE FROM audits WHERE target=${monitor.id}`;
          await sql`DELETE FROM users WHERE id=${monitor.id}`;
        }
      });
      await db.end();
    }
  },
);
