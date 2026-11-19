import {
  defineEventHandler,
  createError,
  readBody,
  getRequestURL,
  getCookie,
  setCookie,
  deleteCookie,
  setHeader,
  getQuery,
  readMultipartFormData,
  createEventStream,
} from "h3";
import { z, ZodError } from "zod";
import { db, audit } from "./db.mjs";
import {
  digest,
  hashPassword,
  verifyPassword,
  newToken,
  mqttHash,
} from "./security.mjs";
import { thresholdSchema, makeCsv } from "./domain.mjs";
import {
  lamps,
  gateways,
  filterLamps,
  reportRows,
  flattenRows,
} from "./queries.mjs";
import {
  brokerInfo,
  provisioningBundle,
  checkCredentials,
} from "./gateway-provisioning.mjs";
import { trashList, changeTrash } from "./trash.mjs";
import { requestTest } from "./individual-tests.mjs";
import { parseDxf } from "./dxf.mjs";
import { pdfReport } from "./report.mjs";
import { TEST_DURATIONS } from "../../shared/schedule.mjs";
// Chart time ranges: window length and aggregation bucket (seconds), ~240-360 points each.
export const SERIES_RANGES = {
  "1h": { seconds: 3600, bucket: 15 },
  "24h": { seconds: 86400, bucket: 300 },
  "7d": { seconds: 7 * 86400, bucket: 1800 },
  "28d": { seconds: 28 * 86400, bucket: 7200 },
  "3m": { seconds: 90 * 86400, bucket: 21600 },
  "1y": { seconds: 365 * 86400, bucket: 86400 },
};
// Fixed whitelist: these names are interpolated into SQL.
const SERIES_FIELDS = [
  "voltage_mv",
  "current_ma",
  "temperature_centi_c",
  "power_mw",
];
const fail = (code, message) => {
  throw createError({ statusCode: code, statusMessage: message });
};
const uuid = z.string().uuid(),
  name = z.string().trim().min(1).max(160),
  pass = z.string().min(12).max(128);
const safeUser = (u) => ({
  id: u.id,
  name: u.name,
  email: u.email,
  role: u.role,
  active: u.active,
});
async function session(event) {
  const token = getCookie(event, "firefly_session");
  if (!token) return null;
  const [u] =
    await db`SELECT u.* FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token_hash=${digest(token)} AND s.expires_at>now() AND u.active`;
  return u || null;
}
export default defineEventHandler(async (event) => {
  setHeader(event, "Cache-Control", "no-store");
  setHeader(event, "X-Content-Type-Options", "nosniff");
  const path = getRequestURL(event)
      .pathname.replace(/^\/api\/?/, "")
      .split("/")
      .filter(Boolean),
    method = event.method;
  try {
    if (!["GET", "HEAD"].includes(method)) {
      const origin = event.node.req.headers.origin;
      const expected = process.env.PUBLIC_ORIGIN || getRequestURL(event).origin;
      if (!origin || origin !== expected) fail(403, "Origen no autorizado");
      const length = Number(event.node.req.headers["content-length"] || 0);
      if (length > 6 * 1024 * 1024) fail(413, "Solicitud demasiado grande");
    }
    if (path.join("/") === "health") return { ok: true };
    if (path.join("/") === "auth/login" && method === "POST") {
      const b = z
        .object({
          email: z
            .string()
            .email()
            .max(254)
            .transform((s) => s.toLowerCase()),
          password: z.string().min(1).max(128),
        })
        .parse(await readBody(event));
      const key = digest(
        b.email + ":" + (event.node.req.socket.remoteAddress || ""),
      );
      const allowed = await db.begin(async (sql) => {
        await sql`INSERT INTO login_attempts(key) VALUES(${key}) ON CONFLICT DO NOTHING`;
        const [a] =
          await sql`SELECT * FROM login_attempts WHERE key=${key} FOR UPDATE`;
        if (Date.now() - new Date(a.window_start).getTime() > 900000) {
          await sql`UPDATE login_attempts SET attempts=1,window_start=now() WHERE key=${key}`;
          return true;
        }
        if (a.attempts >= 10) return false;
        await sql`UPDATE login_attempts SET attempts=attempts+1 WHERE key=${key}`;
        return true;
      });
      if (!allowed) fail(429, "Demasiados intentos; espere 15 minutos");
      const [u] = await db`SELECT * FROM users WHERE email=${b.email}`;
      const valid = verifyPassword(
        b.password,
        u?.password_hash ||
          "00000000000000000000000000000000:" + "00".repeat(64),
      );
      if (!u || !valid || !u.active) fail(401, "Credenciales incorrectas");
      const token = newToken();
      await db`INSERT INTO sessions(token_hash,user_id,expires_at) VALUES(${digest(token)},${u.id},now()+interval '12 hours')`;
      await db`DELETE FROM login_attempts WHERE key=${key}`;
      setCookie(event, "firefly_session", token, {
        httpOnly: true,
        sameSite: "strict",
        secure: process.env.COOKIE_SECURE !== "false",
        path: "/",
        maxAge: 43200,
      });
      return safeUser(u);
    }
    const user = await session(event);
    if (!user) fail(401, "Inicie sesión");
    if (path.join("/") === "auth/me") return safeUser(user);
    if (path.join("/") === "auth/logout" && method === "POST") {
      const token = getCookie(event, "firefly_session");
      await db`DELETE FROM sessions WHERE token_hash=${digest(token)}`;
      deleteCookie(event, "firefly_session", { path: "/" });
      return { ok: true };
    }
    if (!["GET", "HEAD"].includes(method) && user.role !== "admin")
      fail(403, "Solo administrador");
    const [resource, id, action] = path;
    if (resource === "events" && method === "GET") {
      const stream = createEventStream(event);
      let busy = false;
      const tick = async () => {
        if (busy) return;
        busy = true;
        try {
          if (!(await session(event))) {
            await stream.close();
            return;
          }
          const [lampRows, gatewayRows] = await Promise.all([
            lamps(),
            gateways(),
          ]);
          await stream.push({
            event: "snapshot",
            data: JSON.stringify(lampRows),
          });
          await stream.push({
            event: "gateways",
            data: JSON.stringify(gatewayRows),
          });
        } catch {
          await stream.close();
        } finally {
          busy = false;
        }
      };
      const timer = setInterval(tick, 5000);
      stream.onClosed(() => clearInterval(timer));
      void tick();
      return stream.send();
    }
    if (resource === "trash") {
      if (user.role !== "admin") fail(403, "Solo administrador");
      if (method === "GET" && !id) return trashList();
    }
    if (
      ["locations", "gateways", "lamps"].includes(resource) &&
      id &&
      ["trash", "restore"].includes(action) &&
      method === "POST"
    ) {
      return await changeTrash(resource, id, action === "restore", user);
    }
    if (["locations", "gateways", "lamps"].includes(resource) && id) {
      const [record] =
        await db`SELECT deleted_at FROM ${db(resource)} WHERE id::text=${id}`;
      if (record?.deleted_at)
        fail(404, "Registro en papelera; restáurelo desde Administración");
    }
    if (resource === "users") {
      if (user.role !== "admin") fail(403, "Solo administrador");
      if (method === "GET")
        return (await db`SELECT * FROM users ORDER BY created_at`).map(
          safeUser,
        );
      if (method === "POST") {
        const b = z
          .object({
            name,
            email: z
              .string()
              .email()
              .max(254)
              .transform((s) => s.toLowerCase()),
            password: pass,
            role: z.enum(["admin", "monitor"]),
          })
          .parse(await readBody(event));
        const [u] =
          await db`INSERT INTO users(name,email,password_hash,role) VALUES(${b.name},${b.email},${hashPassword(b.password)},${b.role}) RETURNING *`;
        await audit(db, user, "user.create", u.id, { role: u.role });
        return safeUser(u);
      }
      if (method === "PATCH" && id) {
        uuid.parse(id);
        const b = z
          .object({
            name: name.optional(),
            role: z.enum(["admin", "monitor"]).optional(),
            active: z.boolean().optional(),
            password: pass.optional(),
          })
          .strict()
          .parse(await readBody(event));
        return db.begin(async (sql) => {
          await sql`SELECT pg_advisory_xact_lock(91471023)`;
          const [u] = await sql`SELECT * FROM users WHERE id=${id} FOR UPDATE`;
          if (!u) fail(404, "Usuario no encontrado");
          if (
            u.active &&
            u.role === "admin" &&
            (b.active === false || b.role === "monitor")
          ) {
            const admins =
              await sql`SELECT id FROM users WHERE active AND role='admin'`;
            if (admins.length <= 1)
              fail(409, "Debe conservar un administrador activo");
          }
          const [updated] =
            await sql`UPDATE users SET name=${b.name ?? u.name},role=${b.role ?? u.role},active=${b.active ?? u.active},password_hash=${b.password ? hashPassword(b.password) : u.password_hash} WHERE id=${id} RETURNING *`;
          if (b.password || b.active === false || b.role)
            await sql`DELETE FROM sessions WHERE user_id=${id}`;
          await audit(sql, user, "user.update", id, {
            ...b,
            password: b.password ? "restablecida" : undefined,
          });
          return safeUser(updated);
        });
      }
    }
    if (resource === "locations") {
      if (method === "GET")
        return db`SELECT * FROM locations WHERE deleted_at IS NULL ORDER BY kind,name`;
      if (method === "POST" || method === "PATCH") {
        const b = z
          .object({
            name,
            kind: z.enum(["site", "building", "level"]),
            parent_id: uuid.nullable().optional(),
          })
          .parse(await readBody(event));
        if (b.kind === "site" && b.parent_id)
          fail(400, "Una sede no tiene padre");
        if (b.kind !== "site") {
          const [parent] =
            await db`SELECT * FROM locations WHERE id=${b.parent_id || null} AND deleted_at IS NULL`;
          if (parent?.kind !== { building: "site", level: "building" }[b.kind])
            fail(400, "Jerarquía inválida");
        }
        let row;
        if (method === "POST")
          [row] =
            await db`INSERT INTO locations(name,kind,parent_id) VALUES(${b.name},${b.kind},${b.parent_id || null}) RETURNING *`;
        else {
          uuid.parse(id);
          const [old] = await db`SELECT * FROM locations WHERE id=${id}`;
          if (!old) fail(404, "Ubicación no encontrada");
          if (old.kind !== b.kind) fail(400, "No se puede cambiar el tipo");
          [row] =
            await db`UPDATE locations SET name=${b.name},parent_id=${b.parent_id || null} WHERE id=${id} RETURNING *`;
        }
        await audit(db, user, "location.save", row.id, b);
        return row;
      }
    }
    if (resource === "broker" && method === "GET") {
      if (user.role !== "admin") fail(403, "Solo administrador");
      const broker = await brokerInfo();
      if (id === "ca.pem") {
        if (!broker.ca_pem) fail(503, "Certificado del broker no disponible");
        setHeader(event, "Content-Type", "application/x-pem-file");
        setHeader(
          event,
          "Content-Disposition",
          'attachment; filename="firefly-broker-ca.pem"',
        );
        return broker.ca_pem;
      }
      return broker;
    }
    if (resource === "gateways") {
      const gatewayPass = pass.regex(
        /^[\x20-\x7e]+$/,
        "Use caracteres ASCII imprimibles, sin saltos de línea",
      );
      if (method === "POST" && id && action === "verify") {
        const [gateway] =
          await db`SELECT id FROM gateways WHERE id=${id} AND active`;
        if (!gateway) fail(404, "Gateway no encontrado o desactivado");
        const b = z
          .object({ password: gatewayPass })
          .parse(await readBody(event));
        return { credential_status: await checkCredentials(id, b.password) };
      }
      if (method === "GET" && id && action === "connection") {
        if (user.role !== "admin") fail(403, "Solo administrador");
        const [gateway] =
          await db`SELECT id,name,active,report_interval_s FROM gateways WHERE id=${id}`;
        if (!gateway) fail(404, "Gateway no encontrado");
        return provisioningBundle(gateway, null, await brokerInfo(), "unknown");
      }
      if (method === "GET") return gateways();
      if (method === "PATCH" && id && action === "placement") {
        const b = z
          .object({
            level_id: uuid.nullable(),
            x: z.number().finite().min(-1e9).max(1e9).nullable(),
            y: z.number().finite().min(-1e9).max(1e9).nullable(),
          })
          .strict()
          .parse(await readBody(event));
        if ((b.x === null) !== (b.y === null) || (!b.level_id && b.x !== null))
          fail(400, "Coordenadas incompletas o sin nivel");
        return db.begin(async (sql) => {
          if (b.level_id) {
            const [level] =
              await sql`SELECT kind FROM locations WHERE id=${b.level_id} AND deleted_at IS NULL FOR UPDATE`;
            if (level?.kind !== "level")
              fail(400, "Seleccione un nivel para el gateway");
          }
          const [g] =
            await sql`SELECT id,location_id,map_level_id,x,y FROM gateways WHERE id=${id} FOR UPDATE`;
          if (!g) fail(404, "Gateway no encontrado");
          await sql`UPDATE gateways SET map_level_id=${b.level_id},location_id=${b.level_id},x=${b.x},y=${b.y} WHERE id=${id}`;
          await audit(sql, user, "gateway.place", id, { before: g, after: b });
          return { id, map_level_id: b.level_id, x: b.x, y: b.y };
        });
      }
      if (method === "POST") {
        const b = z
            .object({
              id: z
                .string()
                .regex(/^[a-zA-Z0-9_-]{1,64}$/)
                .refine((x) => x !== "firefly-worker"),
              name,
              location_id: uuid.nullable().optional(),
              password: gatewayPass.optional(),
            })
            .parse(await readBody(event)),
          password = b.password || newToken();
        const broker = await brokerInfo();
        const [assignedLocation] = b.location_id
          ? await db`SELECT kind FROM locations WHERE id=${b.location_id} AND deleted_at IS NULL`
          : [];
        const assignedLevel =
          assignedLocation?.kind === "level" ? b.location_id : null;
        const g = await db.begin(async (sql) => {
          const [created] =
            await sql`INSERT INTO gateways(id,name,location_id,map_level_id,mqtt_hash) VALUES(${b.id},${b.name},${b.location_id || null},${assignedLevel},${mqttHash(password)}) RETURNING id,name`;
          await audit(sql, user, "gateway.create", created.id);
          return created;
        });
        const credentialStatus = await checkCredentials(g.id, password);
        return {
          ...g,
          password,
          provisioning: provisioningBundle(
            g,
            password,
            broker,
            credentialStatus,
          ),
        };
      }
      if (method === "PATCH" && id) {
        const b = z
          .object({
            name: name.optional(),
            active: z.boolean().optional(),
            location_id: uuid.nullable().optional(),
            password: gatewayPass.optional(),
            report_interval_s: z.number().int().min(5).max(86400).optional(),
          })
          .parse(await readBody(event));
        const [g] = await db`SELECT * FROM gateways WHERE id=${id}`;
        if (!g) fail(404, "Gateway no encontrado");
        const broker = b.password ? await brokerInfo() : null;
        const moving =
          b.location_id !== undefined && b.location_id !== g.location_id;
        const [newLocation] =
          moving && b.location_id
            ? await db`SELECT kind FROM locations WHERE id=${b.location_id} AND deleted_at IS NULL`
            : [];
        const mapLevel = moving
          ? newLocation?.kind === "level"
            ? b.location_id
            : null
          : g.map_level_id;
        const [updated] =
          await db`UPDATE gateways SET name=${b.name ?? g.name},active=${b.active ?? g.active},location_id=${b.location_id === undefined ? g.location_id : b.location_id},map_level_id=${mapLevel},x=${moving ? null : g.x},y=${moving ? null : g.y},mqtt_hash=${b.password ? mqttHash(b.password) : g.mqtt_hash},report_interval_s=${b.report_interval_s ?? g.report_interval_s},online=CASE WHEN ${b.active === false} THEN false ELSE online END WHERE id=${id} RETURNING id,name,active`;
        await audit(db, user, "gateway.update", id, {
          ...b,
          password: b.password ? "rotada" : undefined,
        });
        if (b.password) {
          const credentialStatus = await checkCredentials(id, b.password);
          return {
            ...updated,
            provisioning: provisioningBundle(
              {
                ...g,
                ...updated,
                report_interval_s: b.report_interval_s ?? g.report_interval_s,
              },
              b.password,
              broker,
              credentialStatus,
            ),
          };
        }
        return updated;
      }
    }
    if (resource === "profiles") {
      if (method === "GET")
        return db`SELECT p.*,v.id version_id,v.version,v.thresholds FROM profiles p JOIN profile_versions v ON v.profile_id=p.id ORDER BY p.name,v.version DESC`;
      if (method === "POST") {
        const b = z
          .object({
            name,
            chemistry: name,
            nominal_mv: z.number().int().positive().max(65535),
            thresholds: thresholdSchema,
          })
          .parse(await readBody(event));
        return db.begin(async (sql) => {
          let pid = id;
          if (id) {
            uuid.parse(id);
            const [p] =
              await sql`SELECT * FROM profiles WHERE id=${id} FOR UPDATE`;
            if (!p) fail(404, "Perfil no encontrado");
            if (p.nominal_mv !== b.nominal_mv || p.chemistry !== b.chemistry)
              fail(
                400,
                "Cree otro perfil para cambiar química o tensión nominal",
              );
          } else {
            const [p] =
              await sql`INSERT INTO profiles(name,chemistry,nominal_mv) VALUES(${b.name},${b.chemistry},${b.nominal_mv}) RETURNING id`;
            pid = p.id;
          }
          const [last] =
            await sql`SELECT COALESCE(max(version),0)::int n FROM profile_versions WHERE profile_id=${pid}`;
          const [v] =
            await sql`INSERT INTO profile_versions(profile_id,version,thresholds) VALUES(${pid},${last.n + 1},${sql.json(b.thresholds)}) RETURNING *`;
          await audit(sql, user, "profile.version", pid, {
            version: v.version,
            thresholds: b.thresholds,
          });
          return v;
        });
      }
    }
    if (resource === "lamps") {
      if (method === "POST" && id && action === "test") {
        uuid.parse(id);
        const b = z
          .object({ request_id: uuid })
          .strict()
          .parse(await readBody(event));
        return await requestTest(id, b.request_id, user);
      }
      if (method === "GET" && !id)
        return filterLamps(await lamps(), getQuery(event));
      if (id && action === "schedule") {
        uuid.parse(id);
        const lamp = (await lamps()).find((x) => x.id === id);
        if (!lamp) fail(404, "Lámpara no encontrada");
        if (method === "DELETE") {
          await db.begin(async (sql) => {
            await sql`DELETE FROM test_schedules WHERE lamp_id=${id}`;
            await audit(sql, user, "lamp.schedule.delete", id);
          });
          return { deleted: true };
        }
        if (method === "PUT") {
          const b = z
            .object({
              enabled: z.boolean(),
              days: z
                .array(z.number().int().min(1).max(7))
                .min(1, "Selecciona al menos un día")
                .max(7)
                .transform((d) => [...new Set(d)].sort()),
              time: z.string().regex(/^([01]\d|2[0-3]):[0-5]\d$/, "Hora inválida"),
              duration_s: z
                .number()
                .int()
                .refine((v) => TEST_DURATIONS.includes(v), "Duración no permitida"),
            })
            .strict()
            .parse(await readBody(event));
          const max = lamp.gateway_test_max_duration_s ?? 30;
          if (b.duration_s > max)
            fail(
              409,
              `El firmware del gateway admite pruebas de hasta ${max} s; actualízalo para usar ${b.duration_s} s`,
            );
          return db.begin(async (sql) => {
            const [row] = await sql`
              INSERT INTO test_schedules(lamp_id,enabled,days,time_local,duration_s,updated_by,updated_at)
              VALUES(${id},${b.enabled},${b.days},${b.time},${b.duration_s},${user.id},now())
              ON CONFLICT(lamp_id) DO UPDATE SET enabled=EXCLUDED.enabled,days=EXCLUDED.days,
                time_local=EXCLUDED.time_local,duration_s=EXCLUDED.duration_s,
                updated_by=EXCLUDED.updated_by,updated_at=now()
              RETURNING *`;
            await audit(sql, user, "lamp.schedule.save", id, b);
            return row;
          });
        }
      }
      if (method === "GET" && id && action === "series") {
        uuid.parse(id);
        if (!(await lamps()).some((x) => x.id === id))
          fail(404, "Lámpara no encontrada");
        const range = z
          .enum(Object.keys(SERIES_RANGES))
          .default("24h")
          .parse(getQuery(event).range);
        const { seconds, bucket } = SERIES_RANGES[range];
        const points = await db`
          SELECT to_timestamp(floor(extract(epoch FROM received_at)/${bucket})*${bucket}) t,
            count(*)::int n,
            ${db.unsafe(
              SERIES_FIELDS.map(
                (f) =>
                  `avg((data->>'${f}')::float8) "${f}", min((data->>'${f}')::float8) "${f}_min", max((data->>'${f}')::float8) "${f}_max"`,
              ).join(","),
            )}
          FROM measurements
          WHERE lamp_id=${id}
            AND received_at >= now() - make_interval(secs => ${seconds})
            AND ((data->>'flags')::int & 8) = 8
          GROUP BY 1 ORDER BY 1`;
        return { range, seconds, bucket, now: new Date(), points };
      }
      if (method === "GET" && id) {
        uuid.parse(id);
        const lamp = (await lamps()).find((x) => x.id === id);
        if (!lamp) fail(404, "Lámpara no encontrada");
        const measurements =
          await db`SELECT * FROM measurements WHERE lamp_id=${id} ORDER BY id DESC LIMIT 360`;
        const tests =
          await db`SELECT * FROM test_reports WHERE lamp_id=${id} ORDER BY id DESC LIMIT 100`;
        const commands =
          await db`SELECT c.*,p.name profile_name,p.nominal_mv profile_nominal_mv,v.version profile_version FROM commands c JOIN profile_versions v ON v.id=c.profile_version_id JOIN profiles p ON p.id=v.profile_id WHERE lamp_id=${id} ORDER BY c.created_at DESC LIMIT 50`;
        const test_requests =
          await db`SELECT * FROM test_requests WHERE lamp_id=${id} ORDER BY created_at DESC LIMIT 30`;
        const [schedule] =
          await db`SELECT enabled,days,to_char(time_local,'HH24:MI') AS time,duration_s,timezone,updated_at FROM test_schedules WHERE lamp_id=${id}`;
        const schedule_runs =
          await db`SELECT r.slot,r.status,r.duration_s,r.reason,r.attempts,r.request_id,t.status request_status,t.result->>'result' result FROM test_schedule_runs r LEFT JOIN test_requests t ON t.id=r.request_id WHERE r.lamp_id=${id} ORDER BY r.slot DESC LIMIT 30`;
        return {
          lamp,
          measurements,
          tests,
          commands,
          test_requests,
          schedule: schedule || null,
          schedule_runs,
        };
      }
      if (method === "POST" && !id) {
        const b = z
          .object({
            gateway_id: name,
            device_id: z.string().regex(/^[a-zA-Z0-9_-]{1,64}$/),
            name,
          })
          .parse(await readBody(event));
        const [l] =
          await db`INSERT INTO lamps(gateway_id,device_id,name) VALUES(${b.gateway_id},${b.device_id},${b.name}) RETURNING *`;
        await audit(db, user, "lamp.create", l.id);
        return l;
      }
      if (method === "PATCH" && id) {
        uuid.parse(id);
        const b = z
          .object({
            name,
            model: z.string().max(160).default(""),
            nominal_mv: z
              .number()
              .int()
              .positive()
              .max(65535)
              .nullable()
              .optional(),
            location_id: uuid.nullable(),
            place: z.string().max(250).default(""),
            x: z.number().finite().min(-1e9).max(1e9).nullable().optional(),
            y: z.number().finite().min(-1e9).max(1e9).nullable().optional(),
          })
          .parse(await readBody(event));
        if (b.location_id) {
          const [loc] =
            await db`SELECT kind FROM locations WHERE id=${b.location_id} AND deleted_at IS NULL`;
          if (loc?.kind !== "level") fail(400, "Seleccione un nivel");
        }
        return db.begin(async (sql) => {
          const [old] =
            await sql`SELECT * FROM lamps WHERE id=${id} FOR UPDATE`;
          if (!old) fail(404, "Lámpara no encontrada");
          const moving = old.location_id !== b.location_id;
          const [l] =
            await sql`UPDATE lamps SET name=${b.name},model=${b.model},nominal_mv=${b.nominal_mv ?? old.nominal_mv},location_id=${b.location_id},place=${b.place},x=${b.x === undefined ? (moving ? null : old.x) : b.x},y=${b.y === undefined ? (moving ? null : old.y) : b.y} WHERE id=${id} RETURNING *`;
          await audit(sql, user, "lamp.update", id, {
            before: {
              location_id: old.location_id,
              place: old.place,
              x: old.x,
              y: old.y,
            },
            after: b,
          });
          return l;
        });
      }
      if (method === "POST" && id && action === "configure") {
        uuid.parse(id);
        const b = z
          .object({ profile_version_id: uuid })
          .parse(await readBody(event));
        return db.begin(async (sql) => {
          const [l] =
            await sql`SELECT l.*,g.active gateway_active FROM lamps l JOIN gateways g ON g.id=l.gateway_id WHERE l.id=${id} AND l.deleted_at IS NULL AND g.deleted_at IS NULL FOR UPDATE OF l`;
          if (!l) fail(404, "Lámpara no encontrada");
          if (!l.config_capable) fail(409, "Firmware sin configuración remota");
          if (!l.gateway_active) fail(409, "Gateway desactivado");
          const [v] =
            await sql`SELECT v.*,p.nominal_mv FROM profile_versions v JOIN profiles p ON p.id=v.profile_id WHERE v.id=${b.profile_version_id}`;
          if (!v) fail(404, "Perfil no encontrado");
          if (!l.nominal_mv || l.nominal_mv !== v.nominal_mv)
            fail(
              409,
              "Configure la tensión nominal de la lámpara y seleccione un perfil compatible",
            );
          const version = l.desired_version + 1;
          await sql`UPDATE commands SET status='superseded' WHERE lamp_id=${id} AND status IN ('pending','sent','deferred')`;
          const [c] =
            await sql`INSERT INTO commands(lamp_id,version,profile_version_id) VALUES(${id},${version},${v.id}) RETURNING *`;
          await sql`UPDATE lamps SET desired_version=${version},profile_version_id=${v.id} WHERE id=${id}`;
          await audit(sql, user, "lamp.configure", id, {
            previous_version: l.desired_version,
            version,
            thresholds: v.thresholds,
          });
          return c;
        });
      }
    }
    if (resource === "plans") {
      if (method === "GET" && !id)
        return db`SELECT id,level_id,filename,warnings,active,created_at FROM plans WHERE (active OR ${user.role === "admin"}) AND level_id IN (SELECT id FROM locations WHERE deleted_at IS NULL) ORDER BY created_at DESC`;
      if (method === "GET" && id) {
        uuid.parse(id);
        const [p] = await db`SELECT * FROM plans WHERE id=${id}`;
        if (
          !p ||
          !(
            await db`SELECT id FROM locations WHERE id=${p.level_id} AND deleted_at IS NULL`
          ).length ||
          (!p.active && user.role !== "admin")
        )
          fail(404, "Plano no encontrado");
        if (action === "download") {
          setHeader(event, "Content-Type", "application/dxf");
          setHeader(
            event,
            "Content-Disposition",
            'attachment; filename="plano.dxf"',
          );
          return p.source;
        }
        delete p.source;
        return p;
      }
      if (method === "POST" && !id) {
        const parts = await readMultipartFormData(event);
        const file = parts?.find((x) => x.name === "file"),
          level = parts?.find((x) => x.name === "level_id")?.data.toString();
        uuid.parse(level);
        if (!file) fail(400, "Archivo obligatorio");
        const [loc] =
          await db`SELECT kind FROM locations WHERE id=${level} AND deleted_at IS NULL`;
        if (loc?.kind !== "level") fail(400, "Nivel inválido");
        const source = file.data.toString("utf8"),
          geometry = parseDxf(source);
        const [p] =
          await db`INSERT INTO plans(level_id,filename,source,geometry,warnings) VALUES(${level},${(file.filename || "plano.dxf").slice(0, 160)},${source},${db.json(geometry)},${db.json(geometry.warnings)}) RETURNING id,level_id,filename,warnings,active`;
        await audit(db, user, "plan.upload", p.id);
        return p;
      }
      if (method === "POST" && id && action === "activate") {
        uuid.parse(id);
        const b = z
          .object({
            positions: z
              .array(
                z.object({
                  lamp_id: uuid,
                  x: z.number().finite().min(-1e9).max(1e9).nullable(),
                  y: z.number().finite().min(-1e9).max(1e9).nullable(),
                }),
              )
              .max(1000),
            gateway_positions: z
              .array(
                z.object({
                  gateway_id: z.string().regex(/^[a-zA-Z0-9_-]{1,64}$/),
                  x: z.number().finite().min(-1e9).max(1e9).nullable(),
                  y: z.number().finite().min(-1e9).max(1e9).nullable(),
                }),
              )
              .max(1000)
              .optional(),
          })
          .parse(await readBody(event));
        return db.begin(async (sql) => {
          const [p] = await sql`SELECT * FROM plans WHERE id=${id} FOR UPDATE`;
          if (!p) fail(404, "Plano no encontrado");
          const [level] =
            await sql`SELECT id FROM locations WHERE id=${p.level_id} AND deleted_at IS NULL FOR UPDATE`;
          if (!level) fail(409, "Restaure primero el nivel del plano");
          const existing =
            await sql`SELECT id FROM lamps WHERE location_id=${p.level_id} AND deleted_at IS NULL FOR UPDATE`;
          if (
            new Set(b.positions.map((x) => x.lamp_id)).size !==
              existing.length ||
            !existing.every((l) => b.positions.some((x) => x.lamp_id === l.id))
          )
            fail(409, "Revise posiciones de todas las lámparas del nivel");
          const existingGateways =
            await sql`SELECT id FROM gateways WHERE map_level_id=${p.level_id} AND deleted_at IS NULL ORDER BY id`;
          const gatewayPositions = b.gateway_positions || [];
          if (
            new Set(gatewayPositions.map((pos) => pos.gateway_id)).size !==
              gatewayPositions.length ||
            !existingGateways.every((g) =>
              gatewayPositions.some((pos) => pos.gateway_id === g.id),
            )
          )
            fail(409, "Revise posiciones de todos los gateways del nivel");
          const requestedIds = gatewayPositions.map((pos) => pos.gateway_id);
          if (requestedIds.length) {
            const found =
              await sql`SELECT id FROM gateways WHERE id IN ${sql(requestedIds)} AND deleted_at IS NULL ORDER BY id FOR UPDATE`;
            if (found.length !== requestedIds.length)
              fail(400, "Gateway no encontrado");
          }
          for (const pos of gatewayPositions) {
            if ((pos.x === null) !== (pos.y === null))
              fail(400, "Coordenadas incompletas del gateway");
            await sql`UPDATE gateways SET map_level_id=${p.level_id},location_id=${p.level_id},x=${pos.x},y=${pos.y} WHERE id=${pos.gateway_id}`;
          }
          for (const pos of b.positions) {
            if ((pos.x === null) !== (pos.y === null))
              fail(400, "Coordenadas incompletas");
            await sql`UPDATE lamps SET x=${pos.x},y=${pos.y} WHERE id=${pos.lamp_id} AND location_id=${p.level_id}`;
          }
          await sql`UPDATE plans SET active=false WHERE level_id=${p.level_id}`;
          await sql`UPDATE plans SET active=true WHERE id=${id}`;
          await audit(sql, user, "plan.activate", id, b);
          return { ok: true };
        });
      }
    }
    if (resource === "reports" && method === "GET") {
      const q = getQuery(event),
        f = Object.fromEntries(
          [
            "from",
            "to",
            "site",
            "building",
            "level",
            "gateway",
            "lamp",
            "state",
          ]
            .filter((k) => q[k])
            .map((k) => [k, String(q[k])]),
        );
      if (f.state && !["alert", "battery", "testing"].includes(f.state))
        fail(400, "Filtro histórico no compatible");
      const measurements = await reportRows(f, "measurements"),
        tests = await reportRows(f, "tests");
      if (id === "measurements.csv" || id === "tests.csv") {
        setHeader(event, "Content-Type", "text/csv; charset=utf-8");
        setHeader(
          event,
          "Content-Disposition",
          `attachment; filename="firefly-${id}"`,
        );
        return makeCsv(flattenRows(id === "tests.csv" ? tests : measurements));
      }
      if (id === "report.pdf") {
        const selected = await Promise.all([
          f.site ? db`SELECT name FROM locations WHERE id::text=${f.site}` : [],
          f.building
            ? db`SELECT name FROM locations WHERE id::text=${f.building}`
            : [],
          f.level
            ? db`SELECT name FROM locations WHERE id::text=${f.level}`
            : [],
          f.gateway ? db`SELECT name FROM gateways WHERE id=${f.gateway}` : [],
          f.lamp
            ? db`SELECT COALESCE(name,device_id) name FROM lamps WHERE id::text=${f.lamp}`
            : [],
        ]);
        const parts = selected.map((rows) => rows[0]?.name).filter(Boolean);
        if (f.state)
          parts.push(
            {
              alert: "Con alerta",
              battery: "En batería",
              testing: "En prueba",
            }[f.state],
          );
        const pdf = await pdfReport(measurements, tests, f, {
          preparedBy: user.name,
          scope: parts.length ? parts.join(" / ") : "Toda la instalación",
        });
        setHeader(event, "Content-Type", "application/pdf");
        setHeader(
          event,
          "Content-Disposition",
          'attachment; filename="firefly-informe.pdf"',
        );
        return pdf;
      }
      return {
        measurement_count: measurements.length,
        test_count: tests.length,
        measurements: flattenRows(measurements).slice(0, 100),
        tests: flattenRows(tests).slice(0, 100),
        preview_limit: 100,
        filters: f,
      };
    }
    if (resource === "audits" && user.role === "admin" && method === "GET")
      return db`SELECT a.*,u.name actor FROM audits a LEFT JOIN users u ON u.id=a.user_id ORDER BY a.id DESC LIMIT 200`;
    fail(404, "Ruta no encontrada");
  } catch (e) {
    if (e instanceof ZodError)
      fail(
        400,
        e.issues
          .map((x) => x.path.join(".") + ": " + x.message)
          .join("; ")
          .slice(0, 500),
      );
    if (e.statusCode) throw e;
    if (e.code === "23514") fail(409, e.message);
    if (e.code === "23505") fail(409, "El registro ya existe");
    if (e.code === "23503" || e.code === "22P02")
      fail(400, "Referencia inválida");
    if (
      /DXF|geometr|Rango de fechas|El PDF admite|50.000 filas|entidades|bloques|límites/.test(
        e.message,
      )
    )
      fail(400, e.message);
    console.error("API error", e.message);
    fail(500, "Error interno; consulte el registro del servicio");
  }
});
