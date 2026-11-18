import { execFileSync } from "node:child_process";
const origin = process.env.PUBLIC_ORIGIN;
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
  if (!r.ok) throw Error(path + ": " + r.status);
  if (path === "auth/login") cookie = r.headers.get("set-cookie").split(";")[0];
  return r.json();
}
await req("auth/login", "POST", {
  email: process.env.ADMIN_EMAIL,
  password: process.env.ADMIN_PASSWORD,
});
const lamps = (await req("lamps")).filter(
    (l) => l.gateway_id === "sim-gateway",
  ),
  profiles = await req("profiles");
for (const l of lamps) {
  if (!l.desired_version) {
    const p = profiles.find(
      (p) => p.nominal_mv === l.nominal_mv && p.name.startsWith("SIMULACIÓN"),
    );
    await req("lamps/" + l.id + "/configure", "POST", {
      profile_version_id: p.version_id,
    });
  }
}
for (let i = 0; i < 180; i++) {
  const rows = (await req("lamps")).filter(
    (l) => l.gateway_id === "sim-gateway",
  );
  if (
    rows.length === 2 &&
    rows.every(
      (l) =>
        l.applied_version === l.desired_version &&
        l.applied_version > 0 &&
        l.latest?.config_version === l.applied_version,
    )
  ) {
    console.log(
      "Ambos perfiles sintéticos confirmados, con revisión en telemetría.",
    );
    break;
  }
  if (i === 179) throw Error("No se confirmó configuración en 90 s");
  await new Promise((r) => setTimeout(r, 500));
}
execFileSync(
  "docker",
  ["compose", "--profile", "simulation", "restart", "simulator"],
  { stdio: "pipe" },
);
for (let i = 0; i < 40; i++) {
  const rows = (await req("lamps")).filter(
    (l) => l.gateway_id === "sim-gateway",
  );
  if (
    rows.every(
      (l) =>
        l.connected &&
        l.latest?.config_version === l.applied_version &&
        l.latest?.uptime_s <= 10,
    )
  ) {
    console.log(
      "Reinicio del simulador: revisiones conservadas en volumen y nueva telemetría recibida.",
    );
    break;
  }
  if (i === 39) throw Error("No se verificó reinicio del simulador");
  await new Promise((r) => setTimeout(r, 500));
}
