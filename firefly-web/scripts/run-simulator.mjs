import { readFile } from "node:fs/promises";
const data = JSON.parse(await readFile(".runtime/demo.json", "utf8"));
process.env.SIM_GATEWAY = data.gateway;
process.env.SIM_PASSWORD = data.password;
await import("../services/simulator.mjs");
