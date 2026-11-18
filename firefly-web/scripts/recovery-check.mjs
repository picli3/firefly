// Local Compose verification only; pauses this checkout's worker, never a gateway.
import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";
import { randomUUID } from "node:crypto";
import mqtt from "mqtt";
import { db } from "../services/lib/db.mjs";
const demo = JSON.parse(await readFile(".runtime/demo.json", "utf8"));
const topic = `firefly/v1/gateways/${demo.gateway}/telemetry`,
  device = "recovery-" + randomUUID().slice(0, 8),
  ids = [];
const client = await mqtt.connectAsync(process.env.MQTT_URL, {
  username: demo.gateway,
  password: demo.password,
  reconnectPeriod: 0,
});
try {
  execFileSync("docker", ["compose", "stop", "worker"], { stdio: "pipe" });
  for (let i = 0; i < 5; i++) {
    const id = randomUUID();
    ids.push(id);
    await client.publishAsync(
      topic,
      JSON.stringify({
        schema_version: 1,
        message_id: id,
        device_id: device,
        boot_id: "recovery-boot",
        sequence: i,
        uptime_s: i + 1,
        live: false,
        report_interval_s: 5,
        voltage_mv: 6000,
        current_ma: 20,
        power_mw: 120,
        temperature_centi_c: 2500,
        soh_pct: 50,
        flags: 9,
        config_capable: false,
      }),
      { qos: 1 },
    );
  }
  execFileSync("docker", ["compose", "start", "worker"], { stdio: "pipe" });
  let found = 0;
  for (let i = 0; i < 30; i++) {
    const [row] =
      await db`SELECT count(*)::int n FROM measurements m JOIN lamps l ON l.id=m.lamp_id WHERE l.gateway_id=${demo.gateway} AND l.device_id=${device}`;
    found = row.n;
    if (found === 5) break;
    await new Promise((r) => setTimeout(r, 500));
  }
  if (found !== 5) throw Error(`Solo ${found} de 5 mensajes recuperados`);
  const [lamp] =
    await db`SELECT * FROM lamps WHERE gateway_id=${demo.gateway} AND device_id=${device}`;
  if (lamp.last_seen !== null)
    throw Error("Backlog no debe marcar conexión actual");
  await db.begin(async (sql) => {
    await sql`DELETE FROM measurements WHERE lamp_id=${lamp.id}`;
    await sql`DELETE FROM lamps WHERE id=${lamp.id}`;
    await sql`DELETE FROM messages WHERE gateway_id=${demo.gateway} AND message_id IN ${sql(ids)}`;
  });
  console.log(
    "Recuperación correcta: 5/5 mensajes QoS1 recibidos tras reiniciar worker; backlog no marca conexión.",
  );
} finally {
  execFileSync("docker", ["compose", "start", "worker"], { stdio: "pipe" });
  await client.endAsync();
  await db.end();
}
