import { createError } from "h3";
import { db, audit } from "./db.mjs";
import { lamps } from "./queries.mjs";
const fail = (message) => {
  throw createError({ statusCode: 409, statusMessage: message });
};
// Margen tras la prueba para que el resultado llegue desde el H2 (igual que el C6).
const RESULT_MARGIN_S = 150;
/**
 * Solicita una prueba de batería. `user` es null para pruebas programadas.
 * @param {{durationS?: number, source?: "manual" | "schedule"}} [opts]
 */
export async function requestTest(lampId, requestId, user, opts = {}) {
  const durationS = opts.durationS ?? 30,
    source = opts.source ?? "manual";
  return db.begin(async (sql) => {
    const [lamp] =
      await sql`SELECT gateway_id FROM lamps WHERE id=${lampId} AND deleted_at IS NULL`;
    if (!lamp)
      throw createError({
        statusCode: 404,
        statusMessage: "Lámpara no encontrada",
      });
    await sql`SELECT id FROM gateways WHERE id=${lamp.gateway_id} FOR UPDATE`;
    await sql`SELECT id FROM lamps WHERE id=${lampId} AND deleted_at IS NULL FOR UPDATE`;
    const [existing] =
      await sql`SELECT * FROM test_requests WHERE id=${requestId}`;
    if (existing) {
      if (existing.lamp_id !== lampId)
        fail("Identificador de solicitud ya utilizado");
      return existing;
    }
    await expireTests(sql);
    const l = (await lamps()).find((l) => l.id === lampId);
    if (!l?.connected) fail("La lámpara o su gateway están sin comunicación");
    if (!l.gateway_test_capable)
      fail(
        "El gateway necesita firmware con pruebas MQTT y reloj sincronizado",
      );
    if (l.testing) fail("La lámpara ya está en prueba");
    if (!l.ac || !l.sensor_valid)
      fail("Se necesita alimentación AC y una lectura válida del sensor");
    if (durationS > (l.gateway_test_max_duration_s ?? 30))
      fail(
        `El firmware del gateway admite pruebas de hasta ${l.gateway_test_max_duration_s ?? 30} s`,
      );
    const [busy] =
      await sql`SELECT id FROM test_requests WHERE gateway_id=${lamp.gateway_id} AND (status IN ('pending','sent','accepted') OR (status='unknown' AND deadline_at>now()))`;
    if (busy)
      fail(
        "Hay una prueba individual pendiente en este gateway; espere su resultado o vencimiento",
      );
    const [free] =
      await sql`SELECT n FROM generate_series(32768,65535) n WHERE NOT EXISTS (SELECT 1 FROM test_requests WHERE gateway_id=${lamp.gateway_id} AND test_id=n) LIMIT 1`;
    if (!free)
      fail(
        "Se agotaron los identificadores de pruebas de este gateway; se requiere una ampliación del protocolo",
      );
    const [row] =
      await sql`INSERT INTO test_requests(id,lamp_id,gateway_id,requested_by,test_id,duration_s,source,deadline_at) VALUES(${requestId},${lampId},${lamp.gateway_id},${user?.id || null},${free.n},${durationS},${source},clock_timestamp()+make_interval(secs => ${durationS + RESULT_MARGIN_S})) RETURNING *`;
    await audit(
      sql,
      user,
      source === "schedule" ? "lamp.test.schedule.run" : "lamp.test.request",
      lampId,
      { command_id: requestId, duration_s: durationS },
    );
    return row;
  });
}
export async function expireTests(sql = db) {
  await sql`UPDATE test_requests SET status='expired',reason='La orden venció antes de enviarse' WHERE status='pending' AND expires_at<now()`;
  await sql`UPDATE test_requests SET status='unknown',reason='Sin resultado confirmado; no se vuelve a ejecutar automáticamente' WHERE status IN ('sent','accepted') AND deadline_at<now()`;
}
export async function dispatchTests(client) {
  await expireTests();
  // Claim BEFORE publishing. A crash or ambiguous PUBACK must never repeat a physical test.
  const rows =
    await db`UPDATE test_requests r SET status='sent',sent_at=now() FROM lamps l,gateways g WHERE r.lamp_id=l.id AND r.gateway_id=g.id AND r.status='pending' AND r.expires_at>now() AND l.deleted_at IS NULL AND g.deleted_at IS NULL AND g.active AND g.online AND g.test_capable AND l.last_seen>=now()-3*l.report_interval_s*interval '1 second' AND g.last_seen>=now()-3*g.report_interval_s*interval '1 second' RETURNING r.*,l.device_id`;
  for (const r of rows) {
    try {
      await client.publishAsync(
        `firefly/v1/gateways/${r.gateway_id}/commands/test`,
        JSON.stringify({
          schema_version: 1,
          command_id: r.id,
          device_id: r.device_id,
          test_id: r.test_id,
          duration_s: r.duration_s,
          issued_at_ms: new Date(r.created_at).getTime(),
          expires_at_ms: new Date(r.expires_at).getTime(),
        }),
        { qos: 1, retain: false },
      );
    } catch {
      await db`UPDATE test_requests SET status='unknown',reason='Envío incierto; no reenviar automáticamente' WHERE id=${r.id} AND status='sent'`;
    }
  }
}
export async function ingestTestAck(sql, gateway, data) {
  const [r] =
    await sql`SELECT * FROM test_requests WHERE id=${data.command_id} AND gateway_id=${gateway} FOR UPDATE`;
  if (!r || r.test_id !== data.test_id) return { ignored: true };
  const [l] = await sql`SELECT device_id FROM lamps WHERE id=${r.lamp_id}`;
  if (l?.device_id !== data.device_id) return { ignored: true };
  if (["sent", "accepted", "unknown"].includes(r.status))
    await sql`UPDATE test_requests SET status=${data.status},acknowledged_at=now(),reason=${data.reason || null} WHERE id=${r.id} AND (status<>'unknown' OR deadline_at>now())`;
  return { accepted: true };
}
export async function ingestIndividualResult(sql, gateway, lamp, data) {
  if (!data.command_id) return;
  const [r] =
    await sql`SELECT * FROM test_requests WHERE id=${data.command_id} AND gateway_id=${gateway} AND lamp_id=${lamp.id} AND test_id=${data.test_id} FOR UPDATE`;
  if (!r || !["sent", "accepted", "unknown"].includes(r.status)) return;
  await sql`UPDATE test_requests SET status='completed',completed_at=COALESCE(completed_at,now()),result=${sql.json(data)},reason=NULL WHERE id=${r.id}`;
}
