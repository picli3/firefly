import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { buildReportModel } from "../services/lib/report-model.mjs";
import { pdfReport } from "../services/lib/report.mjs";
const range = { from: "2026-01-01T05:00:00Z", to: "2026-12-31T23:59:59Z" };
function measurement(date, v = 6000, flags = 9) {
  return {
    lamp_id: "lamp",
    device_id: "node",
    name: "Lámpara de prueba",
    gateway_id: "gateway",
    received_at: new Date(date),
    measured_at: new Date(date),
    config_version: 1,
    profile_snapshot: { name: "SLA 6 V" },
    location_snapshot: { site: "Sede histórica", level: "Nivel histórico" },
    data: { voltage_mv: v, current_ma: -30, temperature_centi_c: 2500, flags },
  };
}
function trial(date, result) {
  return {
    ...measurement(date),
    data: { test_id: 1, result, duration_s: 30, voltage_min_mv: 5900 },
  };
}
test("meses Lima, promedios ponderados y separación de unidades", () => {
  const rows = [
    measurement("2026-02-01T04:59:59Z", 6000),
    measurement("2026-02-01T05:00:00Z", 4000),
    measurement("2026-02-10T12:00:00Z", 5000),
  ];
  const m = buildReportModel(rows, [], range);
  assert.equal(m.years[0].months[0].voltage, 6);
  assert.equal(m.years[0].months[1].voltage, 4.5);
  assert.equal(m.years[0].total.voltage, 5);
  assert.equal(m.years[0].total.current, -30);
  assert.equal(m.years[0].total.temperature, 25);
  assert.equal(m.years[0].months[2].voltage, null);
  assert.equal(m.years[0].months[2].passRate, null);
});
test("sensor inválido no aporta ceros y no inventa perfiles ni fecha de medición", () => {
  const invalid = measurement("2026-01-03T12:00:00Z", 0, 0);
  invalid.data.temperature_centi_c = -32768;
  invalid.measured_at = null;
  invalid.profile_snapshot = null;
  const m = buildReportModel(
    [measurement("2026-01-01T12:00:00Z"), invalid],
    [],
    range,
  );
  assert.equal(m.total.measurements, 2);
  assert.equal(m.total.valid, 1);
  assert.equal(m.total.voltage, 6);
  assert.equal(m.invalid, 1);
  assert.equal(m.receptionOnly, 1);
  assert.equal(m.unknownProfiles, 1);
  assert.equal(m.groups.length, 2);
});
test("PASS recibido, variación en pp, categorías y meses sin reportes", () => {
  const tests = [
    trial("2026-01-01T12:00:00Z", "PASS"),
    trial("2026-01-02T12:00:00Z", "LOW_VOLTAGE"),
    trial("2026-02-01T12:00:00Z", "PASS"),
    trial("2026-04-01T12:00:00Z", "HIGH_TEMPERATURE"),
  ];
  const m = buildReportModel([], tests, range),
    months = m.years[0].months;
  assert.equal(months[0].passRate, 50);
  assert.equal(months[1].variation, 50);
  assert.equal(months[3].variation, null);
  assert.equal(m.total.passRate, 50);
  assert.equal(m.years[0].categories.find((c) => c.key === "OTHER").count, 1);
  assert.equal(m.best.name, "Febrero");
  assert.equal(m.worst.name, "Abril");
});
test("rango vacío, varios años y protección de paginación extrema", () => {
  const empty = buildReportModel([], [], range);
  assert.equal(empty.years.length, 1);
  assert.equal(empty.total.passRate, null);
  assert.equal(empty.best, null);
  const two = buildReportModel([], [], {
    from: "2025-12-01T05:00:00Z",
    to: "2026-01-31T22:00:00Z",
  });
  assert.deepEqual(
    two.years.map((y) => y.year),
    [2025, 2026],
  );
  assert.throws(
    () =>
      buildReportModel([], [], {
        from: "2000-01-01T05:00:00Z",
        to: "2026-01-01T05:00:00Z",
      }),
    /12 años/,
  );
});
test("PDF generado con SVG de la aplicación y fuentes embebidas", async () => {
  const a = await readFile("services/assets/firefly.svg"),
    b = await readFile("public/branding/firefly.svg");
  assert.deepEqual(a, b);
  const bytes = await pdfReport(
    [measurement("2026-01-01T12:00:00Z")],
    [trial("2026-01-01T12:00:00Z", "PASS")],
    range,
    { preparedBy: "Monitor de prueba", scope: "Sede de prueba" },
  );
  assert.equal(bytes.subarray(0, 4).toString(), "%PDF");
  assert.ok(bytes.length > 15000);
});
