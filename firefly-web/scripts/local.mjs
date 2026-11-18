import { spawn } from "node:child_process";
process.loadEnvFile(".env");
process.env.DATABASE_URL ||= `postgres://firefly:${encodeURIComponent(process.env.POSTGRES_PASSWORD)}@127.0.0.1:55432/firefly`;
process.env.MQTT_URL ||= "mqtt://127.0.0.1:51883";
if (!process.env.TEST_HTTPS)
  process.env.PUBLIC_ORIGIN = "http://127.0.0.1:4300";
process.env.COOKIE_SECURE = "false";
process.env.PORT = "4300";
process.env.HOST = "127.0.0.1";
const proc = spawn(process.execPath, process.argv.slice(2), {
  env: process.env,
  stdio: "inherit",
});
proc.on("exit", (c) => process.exit(c || 0));
for (const s of ["SIGTERM", "SIGINT"]) process.on(s, () => proc.kill(s));
