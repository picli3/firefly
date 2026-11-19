import test from "node:test";
import assert from "node:assert/strict";
import {
  nextRun,
  daysText,
  durationText,
  TEST_DURATIONS,
} from "../shared/schedule.mjs";

// Lima = UTC-5 sin horario de verano. 2026-11-18 es miércoles (ISO 3).
const at = (iso) => new Date(iso);

test("próxima ejecución: hoy si la hora no ha pasado", () => {
  const s = { days: [3], time: "20:00", timezone: "America/Lima" };
  assert.equal(
    nextRun(s, at("2026-11-18T12:00:00-05:00")).toISOString(),
    "2026-11-19T01:00:00.000Z",
  );
});

test("próxima ejecución: dentro de la ventana de 15 min sigue siendo hoy", () => {
  const s = { days: [3], time: "08:00" };
  assert.equal(
    nextRun(s, at("2026-11-18T08:10:00-05:00")).toISOString(),
    "2026-11-18T13:00:00.000Z",
  );
});

test("próxima ejecución: pasada la ventana salta al siguiente día marcado", () => {
  const s = { days: [3, 5], time: "08:00" };
  assert.equal(
    nextRun(s, at("2026-11-18T08:16:00-05:00")).toISOString(),
    "2026-11-20T13:00:00.000Z",
  );
});

test("próxima ejecución: misma semana siguiente si solo hay un día", () => {
  const s = { days: [3], time: "08:00" };
  assert.equal(
    nextRun(s, at("2026-11-18T09:00:00-05:00")).toISOString(),
    "2026-11-25T13:00:00.000Z",
  );
});

test("próxima ejecución: cruza medianoche y fin de mes", () => {
  const s = { days: [1], time: "00:30" }; // lunes
  assert.equal(
    nextRun(s, at("2026-11-29T23:50:00-05:00")).toISOString(),
    "2026-11-30T05:30:00.000Z",
  );
});

test("con ventana 0 solo cuenta lo futuro", () => {
  const s = { days: [3, 5], time: "08:00" };
  assert.equal(
    nextRun(s, at("2026-11-18T08:05:00-05:00"), 0).toISOString(),
    "2026-11-20T13:00:00.000Z",
  );
});

test("desactivada o sin días no tiene próxima ejecución", () => {
  assert.equal(nextRun({ enabled: false, days: [1], time: "08:00" }), null);
  assert.equal(nextRun({ days: [], time: "08:00" }), null);
});

test("textos de días y duración", () => {
  assert.equal(daysText([1, 2, 3, 4, 5, 6, 7]), "Todos los días");
  assert.equal(daysText([5, 1, 2, 3, 4]), "Lunes a viernes");
  assert.equal(daysText([1, 3, 5]), "Lunes, miércoles y viernes");
  assert.equal(daysText([3]), "Miércoles");
  assert.equal(daysText([1, 2, 4, 6]), "L, M, J, S");
  assert.equal(durationText(30), "30 s");
  assert.equal(durationText(120), "2 min");
  assert.deepEqual(TEST_DURATIONS, [30, 60, 120, 180, 240, 300]);
});
