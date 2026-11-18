import test from "node:test";
import assert from "node:assert/strict";
import {
  thresholdSchema,
  lampState,
  makeCsv,
  telemetrySchema,
} from "../services/lib/domain.mjs";
import { hashPassword, verifyPassword } from "../services/lib/security.mjs";
import { parseDxf } from "../services/lib/dxf.mjs";
const thresholds = {
  critical_mv: 5250,
  alert_mv: 5500,
  attention_mv: 5850,
  test_min_mv: 5250,
  full_mv: 6300,
  ac_empty_mv: 5500,
  dc_empty_mv: 5250,
};
test("perfil de batería rechaza orden incorrecto y divisor cero", () => {
  assert.equal(thresholdSchema.safeParse(thresholds).success, true);
  assert.equal(
    thresholdSchema.safeParse({ ...thresholds, alert_mv: 6000 }).success,
    false,
  );
  assert.equal(
    thresholdSchema.safeParse({ ...thresholds, ac_empty_mv: 6300 }).success,
    false,
  );
});
test("contraseñas saladas y comprobación segura", () => {
  const a = hashPassword("examplePassword123"),
    b = hashPassword("examplePassword123");
  assert.notEqual(a, b);
  assert.ok(verifyPassword("examplePassword123", a));
  assert.equal(verifyPassword("wrong", a), false);
  assert.equal(verifyPassword("x", "broken"), false);
});
test("CSV preserva números negativos y neutraliza fórmulas en texto", () => {
  const csv = makeCsv([{ name: '=HYPERLINK("bad")', current: -30 }]);
  assert.match(csv, /'=HYPERLINK/);
  assert.match(csv, /"-30"/);
  assert.equal(csv.startsWith("\ufeff"), true);
});
test("caída de gateway y silencio de lámpara son distintos", () => {
  const base = {
    latest: { flags: 17 },
    report_interval_s: 60,
    last_seen: new Date(),
    gateway_last_seen: new Date(),
    gateway_online: true,
    name: "A",
    location_id: "level",
  };
  assert.equal(lampState(base).connected, true);
  assert.equal(
    lampState({ ...base, gateway_online: false }).gateway_connected,
    false,
  );
  assert.equal(
    lampState({ ...base, last_seen: new Date(Date.now() - 181000) })
      .gateway_connected,
    true,
  );
  assert.equal(
    lampState({ ...base, last_seen: new Date(Date.now() - 181000) }).connected,
    false,
  );
  assert.equal(lampState(base).testing, true);
});
test("DXF real con línea y círculo produce geometría acotada", () => {
  const text =
    "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n100\n21\n50\n0\nCIRCLE\n8\n0\n10\n50\n20\n25\n40\n5\n0\nENDSEC\n0\nEOF\n";
  const p = parseDxf(text);
  assert.equal(p.shapes.length, 2);
  assert.deepEqual(p.bounds, [0, 0, 100, 50]);
  assert.throws(() => parseDxf("<svg>bad</svg>"));
});
