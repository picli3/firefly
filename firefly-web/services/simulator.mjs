import mqtt from "mqtt";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import { thresholdSchema } from "./lib/domain.mjs";
const gateway = process.env.SIM_GATEWAY || "sim-gateway",
  password = process.env.SIM_PASSWORD;
if (!password)
  throw Error(
    "SIM_PASSWORD obligatorio: use la contraseña del gateway registrado",
  );
const root = `firefly/v1/gateways/${gateway}`,
  boot = randomUUID();
const lamps = new Map([
  ["sim-4v", { v: 4800, nominal: 4000, config: null, version: 0 }],
  ["sim-6v", { v: 6400, nominal: 6000, config: null, version: 0 }],
]);
const stateFile = process.env.SIM_STATE_FILE || ".runtime/simulator.json";
try {
  const saved = JSON.parse(await readFile(stateFile, "utf8"));
  for (const [id, state] of Object.entries(saved))
    if (lamps.has(id)) Object.assign(lamps.get(id), state);
} catch {}
const client = mqtt.connect(process.env.MQTT_URL || "mqtt://127.0.0.1:51883", {
  username: gateway,
  password,
  clientId: "sim-" + gateway,
  ca: process.env.MQTT_CA ? await readFile(process.env.MQTT_CA) : undefined,
  will: {
    topic: root + "/status",
    payload: JSON.stringify({
      schema_version: 1,
      message_id: randomUUID(),
      online: false,
      boot_id: boot,
    }),
    qos: 1,
    retain: true,
  },
});
let seq = 0,
  uptime = 0;
async function publish(kind, data) {
  return client.publishAsync(
    root + "/" + kind,
    JSON.stringify({ schema_version: 1, message_id: randomUUID(), ...data }),
    { qos: 1, retain: kind === "status" },
  );
}
async function tick() {
  uptime += 5;
  await publish("status", { boot_id: boot, online: true });
  for (const [device_id, l] of lamps) {
    const phase = seq % 24,
      testing = phase >= 18;
    const v = l.v - (testing ? 200 : 0);
    const t = l.config;
    let flags = (testing ? 16 : 1) | 8;
    if (t && v < t.alert_mv) flags |= 2;
    if (t && v < t.critical_mv) flags |= 4;
    const sample = {
      device_id,
      boot_id: boot,
      live: true,
      measured_at: new Date().toISOString(),
      config_version: l.version,
    };
    await publish("telemetry", {
      ...sample,
      sequence: seq % 65536,
      uptime_s: uptime,
      report_interval_s: 5,
      voltage_mv: v,
      current_ma: testing ? 350 : -30,
      power_mw: Math.round(v * (testing ? 0.35 : -0.03)),
      temperature_centi_c: 2600 + (seq % 10) * 5,
      soh_pct: t
        ? Math.max(
            0,
            Math.min(
              100,
              Math.round(
                (100 * (v - (testing ? t.dc_empty_mv : t.ac_empty_mv))) /
                  (t.full_mv - (testing ? t.dc_empty_mv : t.ac_empty_mv)),
              ),
            ),
          )
        : 0,
      flags,
      config_capable: true,
    });
    if (phase === 23)
      await publish("test-report", {
        ...sample,
        test_id: Math.floor(seq / 24),
        result: t && v < t.test_min_mv ? "LOW_VOLTAGE" : "PASS",
        voltage_before_mv: l.v,
        voltage_min_mv: v,
        current_avg_ma: 350,
        current_peak_ma: 390,
        power_avg_mw: Math.round(v * 0.35),
        temperature_max_centi_c: 2700,
        duration_s: 30,
        sample_count: 6,
        flags,
      });
  }
  seq++;
}
client.on("connect", async () => {
  await client.subscribeAsync(root + "/commands/config", { qos: 1 });
  await tick();
  console.log("Simulador conectado:", gateway, "(datos sintéticos)");
});
let commands = Promise.resolve();
client.on("message", (topic, raw) => {
  commands = commands.then(async () => {
    try {
      const c = JSON.parse(raw),
        l = lamps.get(c.device_id);
      if (!l) return;
      if (!Number.isInteger(c.version) || c.version < l.version) return;
      const thresholds = thresholdSchema.parse(c.thresholds);
      if (
        c.version === l.version &&
        JSON.stringify(thresholds) !== JSON.stringify(l.config)
      ) {
        await publish("config-ack", {
          command_id: c.command_id,
          device_id: c.device_id,
          version: c.version,
          status: "rejected",
          reason: "Versión ya aplicada con otros valores",
        });
        return;
      }
      if (seq % 24 >= 18) {
        await publish("config-ack", {
          command_id: c.command_id,
          device_id: c.device_id,
          version: c.version,
          status: "deferred",
          reason: "Prueba simulada activa",
        });
        return;
      }
      l.config = thresholds;
      l.version = c.version;
      const { mkdir, writeFile, rename } = await import("node:fs/promises");
      await mkdir(
        new URL(".", new URL("file://" + process.cwd() + "/" + stateFile)),
        { recursive: true },
      );
      await writeFile(
        stateFile + ".tmp",
        JSON.stringify(Object.fromEntries(lamps)),
      );
      await rename(stateFile + ".tmp", stateFile);
      await publish("config-ack", {
        command_id: c.command_id,
        device_id: c.device_id,
        version: c.version,
        status: "applied",
      });
    } catch (e) {
      console.error("Configuración simulada inválida:", e.message);
    }
  });
});
client.on("error", (e) => console.error(e.message));
const timer = setInterval(() => {
  if (client.connected) tick().catch(console.error);
}, 5000);
async function stop() {
  clearInterval(timer);
  await publish("status", { boot_id: boot, online: false });
  await client.endAsync();
  process.exit();
}
process.on("SIGINT", stop);
process.on("SIGTERM", stop);
