import { z } from "zod";
export const thresholdSchema = z
  .object({
    attention_mv: z.number().int().positive().max(65535),
    alert_mv: z.number().int().positive().max(65535),
    critical_mv: z.number().int().positive().max(65535),
    test_min_mv: z.number().int().positive().max(65535),
    full_mv: z.number().int().positive().max(65535),
    ac_empty_mv: z.number().int().positive().max(65535),
    dc_empty_mv: z.number().int().positive().max(65535),
  })
  .strict()
  .superRefine((t, c) => {
    if (!(
      t.critical_mv <= t.alert_mv &&
      t.alert_mv < t.attention_mv &&
      t.attention_mv <= t.full_mv &&
      t.ac_empty_mv < t.full_mv &&
      t.dc_empty_mv < t.full_mv &&
      t.test_min_mv <= t.full_mv
    ))
      c.addIssue({ code: "custom", message: "Orden de umbrales incoherente" });
  });
const common = {
  schema_version: z.literal(1),
  message_id: z.string().min(1).max(128),
  device_id: z.string().regex(/^[a-zA-Z0-9_-]{1,64}$/),
  boot_id: z.string().min(1).max(128),
  measured_at: z.string().datetime({ offset: true }).optional(),
  live: z.boolean(),
  config_version: z.number().int().nonnegative().optional(),
};
export const telemetrySchema = z
  .object({
    ...common,
    sequence: z.number().int().min(0).max(65535),
    uptime_s: z.number().int().nonnegative(),
    report_interval_s: z.number().int().min(5).max(86400),
    voltage_mv: z.number().int().min(0).max(65535),
    current_ma: z.number().int().min(-32768).max(32767),
    power_mw: z.number().int(),
    temperature_centi_c: z.number().int().min(-32768).max(32767),
    soh_pct: z.number().int().min(0).max(100),
    flags: z.number().int().min(0).max(255),
    config_capable: z.boolean(),
  })
  .strict();
export const testSchema = z
  .object({
    ...common,
    command_id: z.string().uuid().optional(),
    test_id: z.number().int().nonnegative(),
    result: z.enum([
      "PASS",
      "LOW_VOLTAGE",
      "LOW_CURRENT",
      "HIGH_TEMPERATURE",
      "AC_NOT_PRESENT",
      "SENSOR_INVALID",
      "ABORTED",
    ]),
    voltage_before_mv: z.number().int().nonnegative(),
    voltage_min_mv: z.number().int().nonnegative(),
    current_avg_ma: z.number().int(),
    current_peak_ma: z.number().int(),
    power_avg_mw: z.number().int(),
    temperature_max_centi_c: z.number().int(),
    duration_s: z.number().int().min(1).max(300),
    sample_count: z.number().int().min(0).max(255),
    flags: z.number().int().min(0).max(255),
  })
  .strict();
export const ackSchema = z
  .object({
    schema_version: z.literal(1),
    message_id: z.string().min(1).max(128),
    command_id: z.string().uuid(),
    device_id: common.device_id,
    version: z.number().int().positive(),
    status: z.enum(["applied", "deferred", "rejected"]),
    reason: z.string().max(500).optional(),
  })
  .strict();
export const statusSchema = z
  .object({
    schema_version: z.literal(1),
    message_id: z.string().min(1).max(128),
    online: z.boolean(),
    test_capable: z.boolean().optional(),
    boot_id: z.string().min(1).max(128),
  })
  .strict();
export function gatewayState(g, now = Date.now()) {
  const age = g.last_seen
    ? Math.max(0, (now - new Date(g.last_seen).getTime()) / 1000)
    : null;
  return {
    ...g,
    age_s: age,
    connected:
      !!g.active &&
      !!g.online &&
      age !== null &&
      Number.isFinite(age) &&
      age <= 3 * (g.report_interval_s || 60),
  };
}
export function lampState(l, now = Date.now()) {
  const age = l.last_seen
    ? Math.max(0, (now - new Date(l.last_seen).getTime()) / 1000)
    : null;
  const gateway = gatewayState(
    {
      active: true,
      online: l.gateway_online,
      last_seen: l.gateway_last_seen,
      report_interval_s: l.gateway_interval || 60,
    },
    now,
  );
  const connected =
    !!gateway.connected &&
    age !== null &&
    Number.isFinite(age) &&
    age <= 3 * l.report_interval_s;
  const f = l.latest?.flags || 0;
  return {
    ...l,
    age_s: age,
    gateway_age_s: gateway.age_s,
    connected,
    gateway_connected: gateway.connected,
    ac: !!(f & 1),
    alert: !!(f & 2),
    critical: !!(f & 4),
    sensor_valid: !!(f & 8),
    testing: !!(f & 16),
    pending_assignment: !l.name || !l.location_id,
  };
}
export function csvCell(x) {
  if (x === null || x === undefined) return "";
  const s = String(x);
  const safe = /^[\s]*[=+@-]/.test(s) && typeof x !== "number" ? "'" + s : s;
  return '"' + safe.replaceAll('"', '""') + '"';
}
export function makeCsv(rows) {
  if (!rows.length) return '\ufeff"sin_datos"\r\n';
  const cols = Object.keys(rows[0]);
  return (
    "\ufeff" +
    [
      cols.map(csvCell).join(","),
      ...rows.map((r) => cols.map((k) => csvCell(r[k])).join(",")),
    ].join("\r\n") +
    "\r\n"
  );
}

export const testAckSchema = z
  .object({
    schema_version: z.literal(1),
    message_id: z.string().min(1).max(128),
    command_id: z.string().uuid(),
    device_id: common.device_id,
    test_id: z.number().int().min(32768).max(65535),
    status: z.enum(["accepted", "rejected", "unknown"]),
    reason: z.string().max(250).optional(),
  })
  .strict();
