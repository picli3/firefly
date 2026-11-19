import { db } from "./db.mjs";
import { lampState, gatewayState } from "./domain.mjs";
export async function lamps() {
  return (
    await db`SELECT l.*,g.name gateway_name,g.test_capable gateway_test_capable,g.test_max_duration_s gateway_test_max_duration_s,ts.enabled schedule_enabled,ts.days schedule_days,to_char(ts.time_local,'HH24:MI') schedule_time,ts.duration_s schedule_duration_s,ts.timezone schedule_timezone,(g.online AND g.active) gateway_online,g.last_seen gateway_last_seen,g.report_interval_s gateway_interval,loc.name level,b.name building,s.name site,loc.id level_id,b.id building_id,s.id site_id FROM lamps l JOIN gateways g ON g.id=l.gateway_id LEFT JOIN test_schedules ts ON ts.lamp_id=l.id LEFT JOIN locations loc ON loc.id=l.location_id LEFT JOIN locations b ON b.id=loc.parent_id LEFT JOIN locations s ON s.id=b.parent_id WHERE l.deleted_at IS NULL AND g.deleted_at IS NULL ORDER BY l.name NULLS FIRST,l.device_id`
  ).map((l) => lampState(l));
}
export async function gateways() {
  return (
    await db`SELECT id,name,location_id,map_level_id,x,y,active,online,test_capable,last_seen,report_interval_s FROM gateways WHERE deleted_at IS NULL ORDER BY name`
  ).map((g) => gatewayState(g));
}
export function filterLamps(rows, f = {}) {
  return rows.filter(
    (l) =>
      (!f.lamp || l.id === f.lamp) &&
      (!f.gateway || l.gateway_id === f.gateway) &&
      (!f.site || l.site_id === f.site) &&
      (!f.building || l.building_id === f.building) &&
      (!f.level || l.level_id === f.level) &&
      (!f.state ||
        {
          offline: !l.connected,
          alert: l.alert || l.critical,
          testing: l.testing,
          battery: !l.ac && !!l.latest,
          pending: l.pending_assignment,
          scheduled: !!l.schedule_enabled,
        }[f.state]),
  );
}
export async function reportRows(f, kind) {
  const table = kind === "tests" ? "test_reports" : "measurements";
  const from = f.from ? new Date(f.from) : new Date(Date.now() - 86400000),
    to = f.to ? new Date(f.to) : new Date();
  if (!Number.isFinite(+from) || !Number.isFinite(+to) || from > to)
    throw Error("Rango de fechas inválido");
  const rows =
    await db`SELECT r.*,l.name,l.device_id,l.gateway_id FROM ${db(table)} r JOIN lamps l ON l.id=r.lamp_id WHERE COALESCE(r.measured_at,r.received_at)>=${from} AND COALESCE(r.measured_at,r.received_at)<=${to} AND (${f.gateway || null}::text IS NULL OR l.gateway_id=${f.gateway || null}) AND (${f.lamp || null}::text IS NULL OR l.id::text=${f.lamp || null}) AND (${f.site || null}::text IS NULL OR r.location_snapshot->>'site_id'=${f.site || null}) AND (${f.building || null}::text IS NULL OR r.location_snapshot->>'building_id'=${f.building || null}) AND (${f.level || null}::text IS NULL OR r.location_snapshot->>'level_id'=${f.level || null}) ORDER BY COALESCE(r.measured_at,r.received_at),r.id LIMIT 50001`;
  if (rows.length > 50000)
    throw Error("Más de 50.000 filas: reduzca el rango de fechas");
  return rows.filter(
    (r) =>
      !f.state ||
      {
        alert: !!(r.data.flags & 6),
        battery: !(r.data.flags & 1),
        testing: !!(r.data.flags & 16),
      }[f.state],
  );
}
export function flattenRows(rows) {
  return rows.map((r) => ({
    id: r.id,
    lampara: r.name || r.device_id,
    device_id: r.device_id,
    gateway: r.gateway_id,
    fecha_medicion: r.measured_at?.toISOString() || null,
    fecha_recepcion: r.received_at.toISOString(),
    origen_fecha: r.measured_at ? "medicion" : "recepcion",
    sede: r.location_snapshot.site,
    edificio: r.location_snapshot.building,
    nivel: r.location_snapshot.level,
    lugar: r.location_snapshot.place,
    config_version: r.config_version,
    perfil: r.profile_snapshot?.name || "desconocido",
    ...r.data,
  }));
}
