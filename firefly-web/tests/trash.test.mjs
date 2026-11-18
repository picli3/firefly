import test from "node:test";
import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import mqtt from "mqtt";

test(
  "papelera: permisos, dependencias, historial y telemetría sin resurrección",
  { skip: process.env.TRASH_INTEGRATION !== "1" },
  async () => {
    const { db } = await import("../services/lib/db.mjs");
    const { ingest } = await import("../services/lib/ingest.mjs");
    const origin = process.env.PUBLIC_ORIGIN,
      tag = "trash-" + randomUUID().slice(0, 8),
      made = {};
    let cookie = "";
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
    async function mqttAllowed(password) {
      const c = mqtt.connect(process.env.MQTT_URL, {
        username: tag,
        password,
        reconnectPeriod: 0,
        connectTimeout: 1000,
      });
      return new Promise((resolve) => {
        let done = false;
        const finish = (ok) => {
          if (done) return;
          done = true;
          c.end(true);
          resolve(ok);
        };
        c.once("connect", () => finish(true));
        c.once("error", () => finish(false));
        c.once("close", () => finish(false));
      });
    }
    async function expectMqtt(expected) {
      const deadline = Date.now() + 10000;
      while (Date.now() < deadline) {
        if ((await mqttAllowed(made.gw.password)) === expected) return;
        await new Promise((r) => setTimeout(r, 400));
      }
      assert.fail("El broker no actualizó la autorización del gateway");
    }
    try {
      const login = await req("auth/login", "POST", {
        email: process.env.ADMIN_EMAIL,
        password: process.env.ADMIN_PASSWORD,
      });
      assert.equal(login.status, 200);
      cookie = login.cookie;
      assert.equal((await req("trash", "GET", undefined, "")).status, 401);
      made.user = await ok("users", "POST", {
        name: tag,
        email: tag + "@example.invalid",
        password: "testMonitorPassword42",
        role: "monitor",
      });
      const monitor = await req("auth/login", "POST", {
        email: tag + "@example.invalid",
        password: "testMonitorPassword42",
      });
      assert.equal(
        (await req("trash", "GET", undefined, monitor.cookie)).status,
        403,
      );
      made.site = await ok("locations", "POST", {
        name: tag,
        kind: "site",
        parent_id: null,
      });
      made.building = await ok("locations", "POST", {
        name: tag,
        kind: "building",
        parent_id: made.site.id,
      });
      made.level = await ok("locations", "POST", {
        name: tag,
        kind: "level",
        parent_id: made.building.id,
      });
      made.gw = await ok("gateways", "POST", {
        id: tag,
        name: tag,
        location_id: made.level.id,
      });
      await expectMqtt(true);
      made.lamp = await ok("lamps", "POST", {
        gateway_id: tag,
        device_id: "lamp-a",
        name: tag,
      });
      await ok("lamps/" + made.lamp.id, "PATCH", {
        name: tag,
        location_id: made.level.id,
        x: 10,
        y: 20,
      });
      const [plan] =
        await db`INSERT INTO plans(level_id,filename,source,geometry,active) VALUES(${made.level.id},'trash-test.dxf','0\nEOF\n',${db.json({ bounds: [0, 0, 100, 60], shapes: [], warnings: [] })},true) RETURNING id`;
      made.plan = plan;
      const sample = {
        schema_version: 1,
        message_id: randomUUID(),
        device_id: "lamp-a",
        boot_id: tag,
        live: true,
        sequence: 1,
        uptime_s: 10,
        report_interval_s: 5,
        voltage_mv: 4000,
        current_ma: 20,
        power_mw: 80,
        temperature_centi_c: 2500,
        soh_pct: 99,
        flags: 8,
        config_capable: true,
      };
      const topic = `firefly/v1/gateways/${tag}/telemetry`;
      await ingest(topic, Buffer.from(JSON.stringify(sample)));
      for (const resource of ["lamps", "gateways", "locations"]) {
        const id =
          resource === "lamps"
            ? made.lamp.id
            : resource === "gateways"
              ? tag
              : made.level.id;
        for (const action of ["trash", "restore"])
          assert.equal(
            (
              await req(
                `${resource}/${id}/${action}`,
                "POST",
                undefined,
                monitor.cookie,
              )
            ).status,
            403,
          );
      }
      assert.equal(
        (await req("locations/" + made.level.id + "/trash", "POST")).status,
        409,
      );
      assert.equal(
        (await req("gateways/" + tag + "/trash", "POST")).status,
        409,
      );
      await ok("lamps/" + made.lamp.id + "/trash", "POST");
      await ok("lamps/" + made.lamp.id + "/trash", "POST");
      assert.equal((await req("lamps/" + made.lamp.id)).status, 404);
      assert.ok(!(await ok("lamps")).some((l) => l.id === made.lamp.id));
      const ignored = await ingest(
        topic,
        Buffer.from(
          JSON.stringify({
            ...sample,
            message_id: randomUUID(),
            sequence: 2,
            uptime_s: 11,
          }),
        ),
      );
      assert.equal(ignored.ignored, true);
      const [count] =
        await db`SELECT count(*)::int n FROM measurements WHERE lamp_id=${made.lamp.id}`;
      assert.equal(count.n, 1);
      assert.equal(
        (
          await req("lamps/" + made.lamp.id + "/configure", "POST", {
            profile_version_id: randomUUID(),
          })
        ).status,
        404,
      );
      const csv = await ok("reports/measurements.csv?lamp=" + made.lamp.id);
      assert.match(csv, /4000/);
      await ok("gateways/" + tag + "/trash", "POST");
      await expectMqtt(false);
      assert.ok(!(await ok("gateways")).some((g) => g.id === tag));
      assert.equal((await req("gateways/" + tag + "/connection")).status, 404);
      await assert.rejects(
        () =>
          ingest(
            topic,
            Buffer.from(
              JSON.stringify({ ...sample, message_id: randomUUID() }),
            ),
          ),
        /Gateway no registrado/,
      );
      await ok("locations/" + made.level.id + "/trash", "POST");
      await ok("locations/" + made.building.id + "/trash", "POST");
      await ok("locations/" + made.site.id + "/trash", "POST");
      assert.ok(!(await ok("locations")).some((l) => l.id === made.level.id));
      assert.ok(!(await ok("plans")).some((p) => p.id === made.plan.id));
      assert.equal((await req("plans/" + made.plan.id)).status, 404);
      assert.equal(
        (await req("plans/" + made.plan.id + "/download")).status,
        404,
      );
      assert.equal(
        (await req("lamps/" + made.lamp.id + "/restore", "POST")).status,
        409,
      );
      assert.equal(
        (await req("gateways/" + tag + "/restore", "POST")).status,
        409,
      );
      assert.equal(
        (await req("locations/" + made.level.id + "/restore", "POST")).status,
        409,
      );
      const trash = await ok("trash");
      assert.equal(
        trash.filter((r) =>
          [
            made.lamp.id,
            tag,
            made.level.id,
            made.building.id,
            made.site.id,
          ].includes(r.id),
        ).length,
        5,
      );
      assert.ok(trash.every((r) => !("mqtt_hash" in r) && !("password" in r)));
      for (const l of [made.site, made.building, made.level])
        await ok("locations/" + l.id + "/restore", "POST");
      await ok("gateways/" + tag + "/restore", "POST");
      await expectMqtt(true);
      await ok("lamps/" + made.lamp.id + "/restore", "POST");
      assert.equal(
        (await req("plans/" + made.plan.id + "/download")).status,
        200,
      );
      const restored = (await ok("lamps/" + made.lamp.id)).lamp;
      assert.equal(restored.x, 10);
      assert.equal(restored.location_id, made.level.id);
      assert.equal(restored.connected, false);
      assert.ok(
        (await ok("audits")).some(
          (a) => a.action === "lamps.restore" && a.target === made.lamp.id,
        ),
      );
    } finally {
      await db.begin(async (sql) => {
        if (made.lamp) {
          for (const table of [
            "measurements",
            "test_reports",
            "commands",
            "retired_boots",
          ])
            await sql`DELETE FROM ${sql(table)} WHERE lamp_id=${made.lamp.id}`;
          await sql`DELETE FROM lamps WHERE id=${made.lamp.id}`;
        }
        if (made.gw) {
          await sql`DELETE FROM messages WHERE gateway_id=${tag}`;
          await sql`DELETE FROM gateways WHERE id=${tag}`;
        }
        if (made.plan) await sql`DELETE FROM plans WHERE id=${made.plan.id}`;
        for (const l of [made.level, made.building, made.site])
          if (l) await sql`DELETE FROM locations WHERE id=${l.id}`;
        await sql`DELETE FROM audits WHERE target=${tag} OR data->>'name'=${tag}`;
        if (made.user) {
          await sql`DELETE FROM audits WHERE target=${made.user.id}`;
          await sql`DELETE FROM users WHERE id=${made.user.id}`;
        }
      });
      await db.end();
    }
  },
);
