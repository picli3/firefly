import mqtt from "mqtt";
import { dispatchTests } from "./lib/individual-tests.mjs";
import { writeFile, readFile, rename, mkdir, chown } from "node:fs/promises";
import { db } from "./lib/db.mjs";
import { ingest, dispatchCommands } from "./lib/ingest.mjs";
import { mqttHash } from "./lib/security.mjs";
import { migrate } from "./migrate.mjs";
await migrate();
if (!process.env.MQTT_PASSWORD) throw Error("MQTT_PASSWORD obligatorio");
const authDir = process.env.MQTT_AUTH_DIR;
let lastAuth = "",
  lastPolicy = "";
const workerHash = mqttHash(process.env.MQTT_PASSWORD);
async function provision() {
  if (!authDir) return;
  const gateways =
    await db`SELECT id,mqtt_hash FROM gateways WHERE active AND deleted_at IS NULL ORDER BY id`;
  const text =
    "firefly-worker:" +
    workerHash +
    "\n" +
    gateways.map((g) => g.id + ":" + g.mqtt_hash).join("\n") +
    "\n";
  const policy = process.env.MQTT_ACL_FILE
    ? await readFile(process.env.MQTT_ACL_FILE, "utf8")
    : null;
  const authChanged = text !== lastAuth;
  const policyChanged = policy !== null && policy !== lastPolicy;
  if (!authChanged && !policyChanged) return;
  await mkdir(authDir, { recursive: true });
  if (authChanged) {
    await writeFile(authDir + "/passwords.tmp", text, { mode: 0o640 });
    await chown(authDir + "/passwords.tmp", 1883, 1883);
    await rename(authDir + "/passwords.tmp", authDir + "/passwords");
  }
  if (policyChanged) {
    await writeFile(authDir + "/acl.tmp", policy, { mode: 0o640 });
    await chown(authDir + "/acl.tmp", 1883, 1883);
    await rename(authDir + "/acl.tmp", authDir + "/acl");
  }
  if (process.env.MQTT_RELOAD_PID)
    process.kill(Number(process.env.MQTT_RELOAD_PID), "SIGHUP");
  lastAuth = text;
  if (policy !== null) lastPolicy = policy;
}
await provision();
const client = mqtt.connect(process.env.MQTT_URL || "mqtt://127.0.0.1:51883", {
  username: "firefly-worker",
  password: process.env.MQTT_PASSWORD,
  clientId: "firefly-ingest",
  reconnectPeriod: 2000,
  clean: false,
  ca: process.env.MQTT_CA ? await readFile(process.env.MQTT_CA) : undefined,
});
let queue = Promise.resolve();
client.on("connect", () => {
  client.subscribe("firefly/v1/gateways/+/+", { qos: 1 });
  console.log("Worker conectado a MQTT");
});
client.on("error", (e) => console.error("MQTT:", e.message));
// MQTT.js calls handleMessage before PUBACK: persist QoS1 before acknowledging.
client.handleMessage = (packet, done) => {
  queue = queue.then(async () => {
    try {
      if (packet.payload.length > 8192) throw Error("Payload demasiado grande");
      await ingest(packet.topic, packet.payload);
      done();
    } catch (e) {
      if (
        e.name === "ZodError" ||
        e instanceof SyntaxError ||
        /Topic inválido|Gateway no registrado|Fecha futura|Payload demasiado/.test(
          e.message,
        )
      ) {
        console.error("Mensaje rechazado:", packet.topic, e.message);
        done();
      } else {
        console.error(
          "Ingestión no confirmada; reinicio para reentrega:",
          e.message,
        );
        process.exit(1);
      }
    }
  });
};
let busy = false;
const timer = setInterval(async () => {
  if (busy) return;
  busy = true;
  try {
    await provision();
    if (client.connected) {
      await dispatchCommands(client);
      await dispatchTests(client);
    }
  } catch (e) {
    console.error("Worker:", e.message);
  } finally {
    busy = false;
  }
}, 3000);
let retentionDay = "";
const cleanup = setInterval(async () => {
  const day = new Date().toISOString().slice(0, 10);
  if (day === retentionDay) return;
  try {
    await db`DELETE FROM measurements WHERE received_at<now()-interval '12 months'`;
    await db`DELETE FROM sessions WHERE expires_at<now()`;
    await db`DELETE FROM login_attempts WHERE window_start<now()-interval '1 day'`;
    retentionDay = day;
  } catch (e) {
    console.error("Retención:", e.message);
  }
}, 60000);
async function stop() {
  clearInterval(timer);
  clearInterval(cleanup);
  await client.endAsync();
  await queue;
  await db.end();
  process.exit();
}
process.on("SIGTERM", stop);
process.on("SIGINT", stop);
