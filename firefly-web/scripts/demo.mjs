// Creates explicitly synthetic fixtures. Never connects to physical lamps.
import { randomBytes } from "node:crypto";
import { mkdir, writeFile } from "node:fs/promises";
const origin = process.env.PUBLIC_ORIGIN || "http://127.0.0.1:4300";
let cookie = "";
async function req(path, method = "GET", body) {
  const r = await fetch(origin + "/api/" + path, {
    method,
    headers: {
      Origin: origin,
      ...(cookie ? { Cookie: cookie } : {}),
      ...(body ? { "Content-Type": "application/json" } : {}),
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!r.ok) throw Error(path + ": " + r.status + " " + (await r.text()));
  if (path === "auth/login") cookie = r.headers.get("set-cookie").split(";")[0];
  return r.json();
}
await req("auth/login", "POST", {
  email: process.env.ADMIN_EMAIL,
  password: process.env.ADMIN_PASSWORD,
});
let locations = await req("locations");
async function location(name, kind, parent_id) {
  let l = locations.find((l) => l.name === name && l.parent_id === parent_id);
  if (!l) {
    l = await req("locations", "POST", { name, kind, parent_id });
    locations.push(l);
  }
  return l;
}
const site = await location("DEMOSTRACIÓN · datos sintéticos", "site", null),
  building = await location("Edificio de demostración", "building", site.id),
  level = await location("Nivel 1 · Simulación", "level", building.id);
let gateways = await req("gateways");
const password = randomBytes(24).toString("hex");
if (!gateways.some((g) => g.id === "sim-gateway"))
  await req("gateways", "POST", {
    id: "sim-gateway",
    name: "Gateway simulado",
    location_id: level.id,
    password,
  });
else await req("gateways/sim-gateway", "PATCH", { password, active: true });
await req("gateways/sim-gateway/placement", "PATCH", {
  level_id: level.id,
  x: 50,
  y: 48,
});
let profiles = await req("profiles");
for (const [nominal, prefix] of [
  [6000, "6 V"],
  [4000, "4 V"],
]) {
  if (!profiles.some((p) => p.name === "SIMULACIÓN " + prefix)) {
    const thresholds =
      nominal === 6000
        ? {
            critical_mv: 5250,
            alert_mv: 5500,
            attention_mv: 5850,
            test_min_mv: 5250,
            full_mv: 6300,
            ac_empty_mv: 5500,
            dc_empty_mv: 5250,
          }
        : {
            critical_mv: 3500,
            alert_mv: 3700,
            attention_mv: 3900,
            test_min_mv: 3500,
            full_mv: 4400,
            ac_empty_mv: 3700,
            dc_empty_mv: 3500,
          };
    await req("profiles", "POST", {
      name: "SIMULACIÓN " + prefix,
      chemistry: "Ejemplo sintético - NO usar en hardware",
      nominal_mv: nominal,
      thresholds,
    });
  }
}
for (const [device_id, nominal_mv, name] of [
  ["sim-4v", 4000, "SIM · Lámpara 4 V"],
  ["sim-6v", 6000, "SIM · Lámpara 6 V"],
]) {
  let lamps = await req("lamps"),
    l = lamps.find(
      (l) => l.gateway_id === "sim-gateway" && l.device_id === device_id,
    );
  if (!l)
    l = await req("lamps", "POST", {
      gateway_id: "sim-gateway",
      device_id,
      name,
    });
  await req("lamps/" + l.id, "PATCH", {
    name,
    model: "Simulador MQTT",
    nominal_mv,
    location_id: level.id,
    place: nominal_mv === 4000 ? "Pasillo norte" : "Salida principal",
    x: nominal_mv === 4000 ? 25 : 75,
    y: 25,
  });
}
const plans = await req("plans");
if (!plans.some((p) => p.level_id === level.id)) {
  const dxf =
    "0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n8\n0\n90\n4\n70\n1\n10\n0\n20\n0\n10\n100\n20\n0\n10\n100\n20\n60\n10\n0\n20\n60\n0\nLINE\n8\n0\n10\n50\n20\n0\n11\n50\n21\n60\n0\nLINE\n8\n0\n10\n0\n20\n40\n11\n100\n21\n40\n0\nENDSEC\n0\nEOF\n";
  const body = new FormData();
  body.append("level_id", level.id);
  body.append("file", new Blob([dxf]), "demostracion.dxf");
  const r = await fetch(origin + "/api/plans", {
    method: "POST",
    headers: { Origin: origin, Cookie: cookie },
    body,
  });
  if (!r.ok) throw Error("DXF " + r.status);
  const p = await r.json();
  const lamps = await req("lamps");
  await req("plans/" + p.id + "/activate", "POST", {
    gateway_positions: (await req("gateways"))
      .filter((g) => g.map_level_id === level.id)
      .map((g) => ({ gateway_id: g.id, x: g.x, y: g.y })),
    positions: lamps
      .filter((l) => l.location_id === level.id)
      .map((l) => ({ lamp_id: l.id, x: l.x, y: l.y })),
  });
}
await mkdir(".runtime", { recursive: true });
await writeFile(
  ".runtime/demo.json",
  JSON.stringify({ gateway: "sim-gateway", password }),
  { mode: 0o600 },
);
console.log(
  "Demostración preparada. Contraseña guardada en .runtime/demo.json (ignorado por Git).",
);
