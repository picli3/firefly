import test from "node:test";
import assert from "node:assert/strict";
import { gatewayState, lampState } from "../services/lib/domain.mjs";
import { mapAppearance } from "../shared/map-state.mjs";
const now = Date.parse("2026-11-17T15:00:00Z");
function lamp(seconds = 5, flags = 9) {
  return lampState(
    {
      name: "Lámpara",
      location_id: "level",
      last_seen: new Date(now - seconds * 1000),
      report_interval_s: 60,
      gateway_online: true,
      gateway_last_seen: new Date(now - 2000),
      gateway_interval: 60,
      latest: { flags },
    },
    now,
  );
}
test("la comunicación decide el color, incluso con alerta o sensor inválido", () => {
  assert.equal(mapAppearance(lamp(5, 15)).color, "#16785c");
  assert.equal(mapAppearance(lamp(5, 0)).label, "Activa");
  const old = lamp(181, 15);
  assert.equal(mapAppearance(old).color, "#85928c");
  assert.equal(mapAppearance(old).label, "Inactiva");
});
test("actividad de otra lámpara o heartbeat de gateway no renueva la última muestra del nodo", () => {
  const stale = lamp(181),
    recent = lamp(1);
  assert.equal(stale.gateway_connected, true);
  assert.equal(mapAppearance(stale).connected, false);
  assert.equal(mapAppearance(recent).connected, true);
  const never = lampState({ ...recent, last_seen: null }, now);
  assert.equal(mapAppearance(never).connected, false);
});
test("caída o desactivación del gateway vuelve inactivas sus lámparas", () => {
  const l = lamp(1);
  assert.equal(
    mapAppearance(lampState({ ...l, gateway_online: false }, now)).connected,
    false,
  );
  const g = gatewayState(
    {
      active: false,
      online: true,
      last_seen: new Date(now),
      report_interval_s: 60,
    },
    now,
  );
  assert.equal(g.connected, false);
  assert.equal(mapAppearance(g, "gateway").label, "Sin conexión");
});
test("el plano vence localmente la actividad aunque se interrumpa el canal SSE", () => {
  assert.equal(mapAppearance(lamp(175), "lamp", 6).connected, false);
  const nearGatewayTimeout = { ...lamp(1), gateway_age_s: 179 };
  assert.equal(mapAppearance(nearGatewayTimeout, "lamp", 2).connected, false);
  const g = gatewayState(
    {
      active: true,
      online: true,
      last_seen: new Date(now - 175000),
      report_interval_s: 60,
    },
    now,
  );
  assert.equal(mapAppearance(g, "gateway", 6).connected, false);
  assert.equal(mapAppearance({ connected: true }, "lamp").connected, false);
});
