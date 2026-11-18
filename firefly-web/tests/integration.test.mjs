import test from "node:test";
import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
const enabled = process.env.INTEGRATION === "1";
test(
  "API, permisos, históricos, perfiles, DXF y reportes sobre PostgreSQL",
  { skip: !enabled },
  async (t) => {
    const { db } = await import("../services/lib/db.mjs");
    const { ingest } = await import("../services/lib/ingest.mjs");
    const origin = process.env.PUBLIC_ORIGIN || "http://127.0.0.1:4300",
      tag = randomUUID().slice(0, 8),
      created = {};
    let cookie = "";
    async function request(path, method = "GET", body, auth = cookie) {
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
        headers: r.headers,
        body: r.headers.get("content-type")?.includes("json")
          ? JSON.parse(text)
          : text,
      };
    }
    const login = await request(
      "auth/login",
      "POST",
      { email: process.env.ADMIN_EMAIL, password: process.env.ADMIN_PASSWORD },
      "",
    );
    assert.equal(login.status, 200, JSON.stringify(login.body));
    cookie = login.headers.get("set-cookie").split(";")[0];
    await t.test("privacidad, Origin y roles", async () => {
      for (const path of [
        "lamps",
        "locations",
        "plans",
        "reports",
        "events",
        "gateways",
        "profiles",
      ])
        assert.equal(
          (await request(path, "GET", undefined, "")).status,
          401,
          path,
        );
      assert.equal(
        (
          await fetch(origin + "/api/locations", {
            method: "POST",
            headers: { Cookie: cookie, "Content-Type": "application/json" },
            body: "{}",
          })
        ).status,
        403,
      );
      const user = await request("users", "POST", {
        name: "Monitor de prueba " + tag,
        email: `test-${tag}@example.invalid`,
        password: "testMonitorPassword42",
        role: "monitor",
      });
      assert.equal(user.status, 200);
      created.user = user.body.id;
      const m = await request(
        "auth/login",
        "POST",
        {
          email: `test-${tag}@example.invalid`,
          password: "testMonitorPassword42",
        },
        "",
      );
      created.monitor = m.headers.get("set-cookie").split(";")[0];
      assert.equal(
        (
          await request(
            "locations",
            "POST",
            { name: "No permitido", kind: "site" },
            created.monitor,
          )
        ).status,
        403,
      );
      assert.equal(
        (await request("users", "GET", undefined, created.monitor)).status,
        403,
      );
    });
    await t.test("jerarquía y descubrimiento", async () => {
      for (const [kind, parent] of [
        ["site", null],
        ["building", "site"],
        ["level", "building"],
      ]) {
        const r = await request("locations", "POST", {
          name: `Prueba ${kind} ${tag}`,
          kind,
          parent_id: parent ? created[parent] : null,
        });
        assert.equal(r.status, 200, JSON.stringify(r.body));
        created[kind] = r.body.id;
      }
      created.gw = "test-" + tag;
      const g = await request("gateways", "POST", {
        id: created.gw,
        name: "Gateway de prueba " + tag,
        location_id: created.level,
        password: "testGatewayPassword42",
      });
      assert.equal(g.status, 200);
      created.gatewayPassword = g.body.password;
      assert.equal(g.body.provisioning.username, created.gw);
      assert.equal(
        g.body.provisioning.broker.at_supported_by_current_firmware,
        false,
      );
      assert.equal(g.body.provisioning.password, g.body.password);
      assert.equal(g.body.provisioning.credential_status, "verified");
      assert.equal(
        (await request("broker", "GET", undefined, created.monitor)).status,
        403,
      );
      assert.equal(
        (
          await request(
            "gateways/" + created.gw + "/connection",
            "GET",
            undefined,
            created.monitor,
          )
        ).status,
        403,
      );
      assert.equal(
        (
          await request(
            "gateways/" + created.gw + "/verify",
            "POST",
            { password: g.body.password },
            created.monitor,
          )
        ).status,
        403,
      );
      const connection = await request(
        "gateways/" + created.gw + "/connection",
      );
      assert.equal(connection.body.password, null);
      assert.equal(connection.body.at_commands, null);
      const gwPlacement = await request(
        "gateways/" + created.gw + "/placement",
        "PATCH",
        { level_id: created.level, x: 10, y: 12 },
      );
      assert.equal(gwPlacement.status, 200);
      const gatewayRow = (await request("gateways")).body.find(
        (g) => g.id === created.gw,
      );
      assert.equal(gatewayRow.map_level_id, created.level);
      assert.equal(gatewayRow.x, 10);
      assert.equal(gatewayRow.y, 12);
      assert.equal(
        (
          await request(
            "gateways/" + created.gw + "/placement",
            "PATCH",
            { level_id: created.level, x: 3, y: 4 },
            created.monitor,
          )
        ).status,
        403,
      );
      assert.equal(
        (
          await request("gateways/" + created.gw + "/placement", "PATCH", {
            level_id: created.site,
            x: 3,
            y: 4,
          })
        ).status,
        400,
      );
      assert.equal(
        (
          await request("gateways/" + created.gw + "/placement", "PATCH", {
            level_id: created.level,
            x: 3,
            y: null,
          })
        ).status,
        400,
      );
      const ca = await request("broker/ca.pem");
      assert.equal(ca.status, 200);
      assert.match(ca.body, /BEGIN CERTIFICATE/);
      const rotated = await request("gateways/" + created.gw, "PATCH", {
        password: "newTestGatewayPassword42",
      });
      assert.equal(
        rotated.body.provisioning.password,
        "newTestGatewayPassword42",
      );
      assert.equal(rotated.body.provisioning.credential_status, "verified");
      assert.equal(
        (
          await request("gateways/" + created.gw + "/verify", "POST", {
            password: "newTestGatewayPassword42",
          })
        ).body.credential_status,
        "verified",
      );
      const sample = {
        schema_version: 1,
        message_id: randomUUID(),
        device_id: "test-node",
        boot_id: "boot-a",
        sequence: 1,
        uptime_s: 10,
        report_interval_s: 5,
        live: true,
        voltage_mv: 6100,
        current_ma: -30,
        power_mw: -183,
        temperature_centi_c: 2600,
        soh_pct: 90,
        flags: 9,
        config_capable: true,
      };
      created.sample = sample;
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify(sample),
      );
      let l = (await request("lamps")).body.find(
        (x) => x.gateway_id === created.gw,
      );
      assert.ok(l);
      created.lamp = l.id;
      assert.equal(l.pending_assignment, true);
      const r = await request("lamps/" + l.id, "PATCH", {
        name: "Lámpara de prueba " + tag,
        model: "Simulada",
        nominal_mv: 6000,
        location_id: created.level,
        place: "Pasillo de prueba",
      });
      assert.equal(r.status, 200);
    });
    await t.test("perfiles, ACK y trazabilidad", async () => {
      const thresholds = {
        critical_mv: 5250,
        alert_mv: 5500,
        attention_mv: 5850,
        test_min_mv: 5250,
        full_mv: 6300,
        ac_empty_mv: 5500,
        dc_empty_mv: 5250,
      };
      assert.equal(
        (
          await request("profiles", "POST", {
            name: "Inválido",
            chemistry: "SLA",
            nominal_mv: 6000,
            thresholds: { ...thresholds, full_mv: 4000 },
          })
        ).status,
        400,
      );
      const p = await request("profiles", "POST", {
        name: "Perfil de prueba " + tag,
        chemistry: "SLA",
        nominal_mv: 6000,
        thresholds,
      });
      assert.equal(p.status, 200);
      created.profile = p.body.profile_id;
      const c = await request("lamps/" + created.lamp + "/configure", "POST", {
        profile_version_id: p.body.id,
      });
      assert.equal(c.status, 200);
      assert.equal(c.body.status, "pending");
      let detail = (await request("lamps/" + created.lamp)).body;
      assert.equal(detail.lamp.applied_version, null);
      const ack = {
        schema_version: 1,
        message_id: randomUUID(),
        command_id: c.body.id,
        device_id: "test-node",
        version: 1,
        status: "deferred",
      };
      await ingest(
        `firefly/v1/gateways/${created.gw}/config-ack`,
        JSON.stringify(ack),
      );
      assert.equal(
        (await request("lamps/" + created.lamp)).body.commands[0].status,
        "deferred",
      );
      await ingest(
        `firefly/v1/gateways/${created.gw}/config-ack`,
        JSON.stringify({ ...ack, message_id: randomUUID(), status: "applied" }),
      );
      detail = (await request("lamps/" + created.lamp)).body;
      assert.equal(detail.lamp.applied_version, 1);
      const s = {
        ...created.sample,
        message_id: randomUUID(),
        sequence: 2,
        uptime_s: 20,
        config_version: 1,
      };
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify(s),
      );
      assert.deepEqual(
        await ingest(
          `firefly/v1/gateways/${created.gw}/telemetry`,
          JSON.stringify(s),
        ),
        { duplicate: true },
      );
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify({
          ...s,
          message_id: randomUUID(),
          uptime_s: 5,
          voltage_mv: 4200,
        }),
      );
      detail = (await request("lamps/" + created.lamp)).body;
      assert.equal(detail.lamp.latest.voltage_mv, 6100);
      assert.equal(
        detail.measurements[0].profile_snapshot.name,
        "Perfil de prueba " + tag,
      );
      const before =
        await db`SELECT location_snapshot FROM measurements WHERE lamp_id=${created.lamp} AND config_version=1 LIMIT 1`;
      assert.equal(before[0].location_snapshot.level_id, created.level);
      await request("lamps/" + created.lamp, "PATCH", {
        name: "Lámpara de prueba " + tag,
        model: "Simulada",
        nominal_mv: 6000,
        location_id: null,
        place: "Nueva ubicación",
      });
      const after =
        await db`SELECT location_snapshot FROM measurements WHERE lamp_id=${created.lamp} AND config_version=1 LIMIT 1`;
      assert.equal(after[0].location_snapshot.level_id, created.level);
      const c2 = await request("lamps/" + created.lamp + "/configure", "POST", {
        profile_version_id: p.body.id,
      });
      const c3 = await request("lamps/" + created.lamp + "/configure", "POST", {
        profile_version_id: p.body.id,
      });
      assert.equal(c3.body.version, 3);
      await ingest(
        `firefly/v1/gateways/${created.gw}/config-ack`,
        JSON.stringify({
          ...ack,
          message_id: randomUUID(),
          command_id: c2.body.id,
          version: 2,
          status: "applied",
        }),
      );
      assert.equal(
        (await request("lamps/" + created.lamp)).body.lamp.applied_version,
        1,
      );
      await ingest(
        `firefly/v1/gateways/${created.gw}/config-ack`,
        JSON.stringify({
          ...ack,
          message_id: randomUUID(),
          command_id: c3.body.id,
          version: 3,
          status: "applied",
        }),
      );
      const final = (await request("lamps/" + created.lamp)).body;
      assert.equal(final.lamp.applied_version, 3);
      assert.equal(
        final.commands.find((c) => c.id === c2.body.id).status,
        "superseded",
      );
    });
    await t.test("DXF privado y activación explícita", async () => {
      const source =
        "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n100\n21\n50\n0\nENDSEC\n0\nEOF\n";
      const data = new FormData();
      data.append("level_id", created.level);
      data.append("file", new Blob([source]), "prueba.dxf");
      const r = await fetch(origin + "/api/plans", {
        method: "POST",
        headers: { Cookie: cookie, Origin: origin },
        body: data,
      });
      const p = await r.json();
      assert.equal(r.status, 200, JSON.stringify(p));
      created.plan = p.id;
      assert.equal(
        (await request("plans/" + p.id, "GET", undefined, created.monitor))
          .status,
        404,
      );
      assert.ok(
        !(await request("plans", "GET", undefined, created.monitor)).body.some(
          (row) => row.id === p.id,
        ),
      );
      assert.equal(p.active, false);
      assert.equal(
        (
          await request("plans/" + p.id + "/activate", "POST", {
            positions: [],
          })
        ).status,
        409,
      );

      assert.equal(
        (await request("plans/" + p.id, "GET", undefined, "")).status,
        401,
      );
      assert.equal(
        (
          await request("plans/" + p.id + "/activate", "POST", {
            positions: [],
            gateway_positions: [{ gateway_id: created.gw, x: 10, y: 12 }],
          })
        ).status,
        200,
      );
      await request("lamps/" + created.lamp, "PATCH", {
        name: "Lámpara de prueba " + tag,
        model: "Simulada",
        nominal_mv: 6000,
        location_id: created.level,
        place: "Posición de prueba",
        x: 25,
        y: 20,
      });
      assert.equal(
        (
          await request("plans/" + p.id + "/activate", "POST", {
            positions: [],
          })
        ).status,
        409,
      );
      assert.equal(
        (
          await request("plans/" + p.id + "/activate", "POST", {
            positions: [{ lamp_id: created.lamp, x: 30, y: 22 }],
            gateway_positions: [{ gateway_id: created.gw, x: 15, y: 18 }],
          })
        ).status,
        200,
      );
      const detail = (await request("lamps/" + created.lamp)).body;
      assert.equal(detail.lamp.x, 30);
      assert.equal(detail.lamp.y, 22);
      const positionedGateway = (await request("gateways")).body.find(
        (g) => g.id === created.gw,
      );
      assert.equal(positionedGateway.map_level_id, created.level);
      assert.equal(positionedGateway.x, 15);
      assert.equal(positionedGateway.y, 18);
    });
    await t.test("reportes para monitor y revocación de sesión", async () => {
      const r = await request(
        "reports?gateway=" + created.gw,
        "GET",
        undefined,
        created.monitor,
      );
      assert.equal(r.status, 200);
      assert.equal(r.body.measurement_count, 3);
      const csv = await request(
        "reports/measurements.csv?gateway=" + created.gw,
        "GET",
        undefined,
        created.monitor,
      );
      assert.equal(csv.status, 200);
      assert.match(csv.body, /Perfil de prueba/);
      assert.equal(
        (
          await request(
            "reports/report.pdf?from=2000-01-01T05:00:00Z&to=2026-11-17T18:00:00Z",
          )
        ).status,
        400,
      );
      const pdf = await fetch(
        origin + "/api/reports/report.pdf?gateway=" + created.gw,
        { headers: { Cookie: created.monitor } },
      );
      assert.equal(pdf.status, 200);
      const bytes = Buffer.from(await pdf.arrayBuffer());
      assert.equal(bytes.subarray(0, 4).toString(), "%PDF");
      await import("node:fs/promises").then((fs) =>
        fs.writeFile("/tmp/firefly-test-report.pdf", bytes),
      );
      const controller = new AbortController();
      const stream = await fetch(origin + "/api/events", {
        headers: { Cookie: created.monitor },
        signal: controller.signal,
      });
      assert.equal(stream.status, 200);
      const reader = stream.body.getReader();
      const first = await reader.read();
      assert.match(Buffer.from(first.value).toString(), /snapshot/);
      assert.equal(
        (await request("users/" + created.user, "PATCH", { active: false }))
          .status,
        200,
      );
      const deadline = setTimeout(() => controller.abort(), 8000);
      let ended = false;
      while (!ended) {
        ended = (await reader.read()).done;
      }
      clearTimeout(deadline);
      assert.equal(
        (await request("lamps", "GET", undefined, created.monitor)).status,
        401,
      );
    });
    await t.test("reinicios y backlog no retroceden el estado", async () => {
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify({
          ...created.sample,
          message_id: randomUUID(),
          boot_id: "boot-b",
          uptime_s: 1,
          voltage_mv: 6200,
        }),
      );
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify({
          ...created.sample,
          message_id: randomUUID(),
          boot_id: "boot-a",
          uptime_s: 100,
          voltage_mv: 4000,
        }),
      );
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify({
          ...created.sample,
          message_id: randomUUID(),
          boot_id: "boot-c",
          uptime_s: 1,
          voltage_mv: 4100,
          live: false,
        }),
      );
      assert.equal(
        (await request("lamps/" + created.lamp)).body.lamp.latest.voltage_mv,
        6200,
      );
    });
    await t.test("actividad por nodo y caída del gateway", async () => {
      await db`UPDATE lamps SET last_seen=now()-interval '16 seconds' WHERE id=${created.lamp}`;
      await db`UPDATE gateways SET online=true,last_seen=now() WHERE id=${created.gw}`;
      let l = (await request("lamps/" + created.lamp)).body.lamp;
      assert.equal(l.connected, false);
      assert.equal(l.gateway_connected, true);
      await ingest(
        `firefly/v1/gateways/${created.gw}/telemetry`,
        JSON.stringify({
          ...created.sample,
          message_id: randomUUID(),
          boot_id: "boot-live",
          uptime_s: 1,
          flags: 15,
        }),
      );
      l = (await request("lamps/" + created.lamp)).body.lamp;
      assert.equal(l.connected, true);
      assert.equal(l.alert, true);
      const g = (await request("gateways")).body.find(
        (g) => g.id === created.gw,
      );
      assert.equal(g.connected, true);
      assert.ok(g.age_s >= 0);
      await db`UPDATE gateways SET online=false WHERE id=${created.gw}`;
      l = (await request("lamps/" + created.lamp)).body.lamp;
      assert.equal(l.connected, false);
      assert.equal(l.gateway_connected, false);
    });
    // All records are synthetic and isolated by the generated gateway ID. Cleanup only these fixtures.
    await db.begin(async (sql) => {
      await sql`DELETE FROM measurements WHERE lamp_id=${created.lamp}`;
      await sql`DELETE FROM test_reports WHERE lamp_id=${created.lamp}`;
      await sql`DELETE FROM commands WHERE lamp_id=${created.lamp}`;
      await sql`DELETE FROM retired_boots WHERE lamp_id=${created.lamp}`;
      await sql`DELETE FROM lamps WHERE id=${created.lamp}`;
      await sql`DELETE FROM messages WHERE gateway_id=${created.gw}`;
      await sql`DELETE FROM gateways WHERE id=${created.gw}`;
      await sql`DELETE FROM plans WHERE id=${created.plan}`;
      await sql`DELETE FROM locations WHERE id IN (${created.level},${created.building},${created.site})`;
      await sql`DELETE FROM profile_versions WHERE profile_id=${created.profile}`;
      await sql`DELETE FROM profiles WHERE id=${created.profile}`;
      await sql`DELETE FROM audits WHERE user_id=${created.user}`;
      await sql`DELETE FROM users WHERE id=${created.user}`;
    });
    await db.end();
  },
);
