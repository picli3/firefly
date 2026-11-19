import { randomUUID } from "node:crypto";
import { db } from "./db.mjs";
import { requestTest } from "./individual-tests.mjs";
import { SCHEDULE_WINDOW_MIN } from "../../shared/schedule.mjs";

const WINDOW = `${SCHEDULE_WINDOW_MIN} minutes`;
// Pausa entre intentos de una misma ocurrencia (lámpara no lista, gateway ocupado…).
const RETRY = "30 seconds";

/**
 * Ejecuta las planificaciones de pruebas. Lo llama el worker periódicamente.
 * 1. Abre una ocurrencia por cada planificación cuya hora local cae en la ventana
 *    de reintento (hoy o ayer, para ventanas que cruzan medianoche).
 * 2. Cierra como 'skipped' las ocurrencias cuya ventana terminó sin solicitud.
 * 3. Intenta crear la solicitud de prueba; si la lámpara o el gateway no están
 *    listos, guarda el motivo y reintenta más tarde dentro de la ventana.
 * Nunca hay más de una solicitud por ocurrencia (status pasa a 'requested').
 */
export async function runSchedules() {
  // Solo ocurrencias posteriores a la última edición: guardar una hora ya pasada
  // no dispara una prueba inmediata.
  await db`
    INSERT INTO test_schedule_runs(lamp_id, slot, duration_s)
    SELECT s.lamp_id, x.slot, s.duration_s
    FROM test_schedules s
    JOIN lamps l ON l.id = s.lamp_id AND l.deleted_at IS NULL
    CROSS JOIN LATERAL (
      SELECT ((((now() AT TIME ZONE s.timezone)::date + d) + s.time_local) AT TIME ZONE s.timezone) AS slot
      FROM generate_series(-1, 0) d
    ) x
    WHERE s.enabled
      AND extract(isodow FROM (x.slot AT TIME ZONE s.timezone))::int = ANY(s.days)
      AND now() >= x.slot
      AND now() < x.slot + ${WINDOW}::interval
      AND x.slot >= s.updated_at
    ON CONFLICT DO NOTHING`;

  await db`
    UPDATE test_schedule_runs
    SET status = 'skipped', updated_at = now(),
        reason = COALESCE(reason, 'No se pudo iniciar dentro de la ventana')
    WHERE status = 'waiting' AND now() >= slot + ${WINDOW}::interval`;

  // Ocurrencias cuya planificación se desactivó, borró o cuya lámpara está en la papelera.
  await db`
    UPDATE test_schedule_runs r
    SET status = 'skipped', updated_at = now(), reason = 'Planificación desactivada o eliminada'
    WHERE r.status = 'waiting' AND NOT EXISTS (
      SELECT 1 FROM test_schedules s JOIN lamps l ON l.id = s.lamp_id
      WHERE s.lamp_id = r.lamp_id AND s.enabled AND l.deleted_at IS NULL
    )`;

  const due = await db`
    -- slot se compara como texto: postgres.js convierte los parámetros de fecha en Date
    -- (milisegundos) y una ocurrencia con microsegundos no volvería a coincidir.
    SELECT lamp_id, slot::text AS slot_key, duration_s FROM test_schedule_runs
    WHERE status = 'waiting'
      AND (attempts = 0 OR updated_at <= now() - ${RETRY}::interval)
    ORDER BY slot
    LIMIT 20`;
  for (const run of due) {
    const requestId = randomUUID();
    try {
      await requestTest(run.lamp_id, requestId, null, {
        durationS: run.duration_s,
        source: "schedule",
      });
      await db`
        UPDATE test_schedule_runs
        SET status = 'requested', request_id = ${requestId}, reason = NULL,
            attempts = attempts + 1, updated_at = now()
        WHERE lamp_id = ${run.lamp_id} AND slot::text = ${run.slot_key} AND status = 'waiting'`;
    } catch (e) {
      const reason = e?.statusMessage || e?.message || "Error desconocido";
      await db`
        UPDATE test_schedule_runs
        SET reason = ${reason}, attempts = attempts + 1, updated_at = now()
        WHERE lamp_id = ${run.lamp_id} AND slot::text = ${run.slot_key} AND status = 'waiting'`;
    }
  }
}
