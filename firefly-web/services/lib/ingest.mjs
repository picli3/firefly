import { ingestTestAck, ingestIndividualResult } from "./individual-tests.mjs";
import { db, locationSnapshot } from "./db.mjs";
import {
  telemetrySchema,
  testSchema,
  ackSchema,
  statusSchema,
  testAckSchema,
} from "./domain.mjs";
export async function ingest(topic, raw) {
  const match =
    /^firefly\/v1\/gateways\/([\w-]{1,64})\/(telemetry|test-report|status|config-ack|test-ack)$/.exec(
      topic,
    );
  if (!match) throw Error("Topic inválido");
  const [, gateway, kind] = match;
  const data = {
    telemetry: telemetrySchema,
    "test-report": testSchema,
    status: statusSchema,
    "test-ack": testAckSchema,
    "config-ack": ackSchema,
  }[kind].parse(JSON.parse(raw.toString()));
  if (
    data.measured_at &&
    new Date(data.measured_at).getTime() > Date.now() + 60000
  )
    throw Error("Fecha futura inválida");
  return db.begin(async (sql) => {
    const [gw] =
      await sql`SELECT * FROM gateways WHERE id=${gateway} AND active=true AND deleted_at IS NULL FOR UPDATE`;
    if (!gw) throw Error("Gateway no registrado o desactivado");
    const inserted =
      await sql`INSERT INTO messages(gateway_id,message_id) VALUES(${gateway},${data.message_id}) ON CONFLICT DO NOTHING RETURNING message_id`;
    if (!inserted.length) return { duplicate: true };
    if (kind === "status") {
      await sql`UPDATE gateways SET online=${data.online},test_capable=${!!data.online && !!data.test_capable},test_max_duration_s=${data.test_max_duration_s ?? 30},last_seen=now(),boot_id=${data.boot_id} WHERE id=${gateway}`;
      return { accepted: true };
    }
    if (kind === "test-ack") return ingestTestAck(sql, gateway, data);
    await sql`INSERT INTO lamps(gateway_id,device_id) VALUES(${gateway},${data.device_id}) ON CONFLICT DO NOTHING`;
    const [lamp] =
      await sql`SELECT * FROM lamps WHERE gateway_id=${gateway} AND device_id=${data.device_id} FOR UPDATE`;
    if (lamp.deleted_at) return { ignored: true, reason: "lamp_in_trash" };
    if (kind === "config-ack") {
      const [cmd] =
        await sql`SELECT * FROM commands WHERE id=${data.command_id} AND lamp_id=${lamp.id} AND version=${data.version} FOR UPDATE`;
      if (
        !cmd ||
        cmd.version !== lamp.desired_version ||
        ["superseded", "applied", "rejected"].includes(cmd.status)
      )
        return { ignored: true };
      await sql`UPDATE commands SET status=${data.status},reason=${data.reason || null},applied_at=${data.status === "applied" ? new Date() : null},next_attempt_at=now()+interval '60 seconds' WHERE id=${cmd.id}`;
      if (data.status === "applied")
        await sql`UPDATE lamps SET applied_version=${data.version} WHERE id=${lamp.id}`;
      return { accepted: true };
    }
    const [profile] =
      data.config_version === undefined
        ? []
        : await sql`SELECT p.name,p.chemistry,p.nominal_mv,v.version,v.thresholds FROM commands c JOIN profile_versions v ON v.id=c.profile_version_id JOIN profiles p ON p.id=v.profile_id WHERE c.lamp_id=${lamp.id} AND c.version=${data.config_version}`;
    const loc = await locationSnapshot(sql, lamp);
    const table = kind === "telemetry" ? "measurements" : "test_reports";
    await sql`INSERT INTO ${sql(table)}(lamp_id,measured_at,data,location_snapshot,config_version,profile_snapshot) VALUES(${lamp.id},${data.measured_at || null},${sql.json(data)},${sql.json(loc)},${data.config_version ?? null},${profile ? sql.json(profile) : null})`;
    if (kind === "test-report")
      await ingestIndividualResult(sql, gateway, lamp, data);
    if (data.live)
      await sql`UPDATE gateways SET online=true,last_seen=now() WHERE id=${gateway}`;
    if (kind === "telemetry") {
      const retired =
        (
          await sql`SELECT 1 FROM retired_boots WHERE lamp_id=${lamp.id} AND boot_id=${data.boot_id}`
        ).length > 0;
      const timedOutdated =
        data.measured_at &&
        lamp.latest_measured_at &&
        new Date(data.measured_at) < new Date(lamp.latest_measured_at);
      const sameBootOld =
        lamp.latest_boot === data.boot_id &&
        lamp.latest_uptime !== null &&
        data.uptime_s <= Number(lamp.latest_uptime);
      // A timestamped packet older than the freshness window is history, even if sent now.
      const stale =
        data.measured_at &&
        Date.now() - new Date(data.measured_at).getTime() >
          3 * data.report_interval_s * 1000;
      if (data.live && !retired && !timedOutdated && !sameBootOld && !stale) {
        if (lamp.latest_boot && lamp.latest_boot !== data.boot_id)
          await sql`INSERT INTO retired_boots(lamp_id,boot_id) VALUES(${lamp.id},${lamp.latest_boot}) ON CONFLICT DO NOTHING`;
        await sql`UPDATE lamps SET latest=${sql.json(data)},last_seen=now(),latest_boot=${data.boot_id},latest_uptime=${data.uptime_s},latest_measured_at=${data.measured_at || null},report_interval_s=${data.report_interval_s},config_capable=${data.config_capable} WHERE id=${lamp.id}`;
      }
    }
    return { accepted: true };
  });
}
export async function dispatchCommands(client) {
  const rows =
    await db`SELECT c.*,l.device_id,l.gateway_id,v.thresholds FROM commands c JOIN lamps l ON l.id=c.lamp_id JOIN gateways g ON g.id=l.gateway_id JOIN profile_versions v ON v.id=c.profile_version_id WHERE c.status IN ('pending','sent','deferred') AND c.next_attempt_at<=now() AND c.version=l.desired_version AND g.active AND g.deleted_at IS NULL AND l.deleted_at IS NULL AND l.config_capable ORDER BY c.created_at LIMIT 30`;
  for (const c of rows) {
    await client.publishAsync(
      `firefly/v1/gateways/${c.gateway_id}/commands/config`,
      JSON.stringify({
        schema_version: 1,
        command_id: c.id,
        device_id: c.device_id,
        version: c.version,
        thresholds: c.thresholds,
      }),
      { qos: 1, retain: false },
    );
    await db`UPDATE commands SET status=CASE WHEN status='pending' THEN 'sent' ELSE status END,attempts=attempts+1,next_attempt_at=now()+${Math.min(300, 30 * (c.attempts + 1))}*interval '1 second' WHERE id=${c.id} AND status IN ('pending','sent','deferred')`;
  }
}
