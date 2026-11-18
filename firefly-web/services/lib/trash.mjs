import { createError } from "h3";
import { db, audit } from "./db.mjs";
const tables = new Set(["locations", "gateways", "lamps"]);
export async function trashList() {
  // Explicit public inventory fields: never expose mqtt_hash or credentials.
  return db`SELECT 'locations' resource,id::text,name,kind,deleted_at FROM locations WHERE deleted_at IS NOT NULL
    UNION ALL SELECT 'gateways',id,name,'gateway',deleted_at FROM gateways WHERE deleted_at IS NOT NULL
    UNION ALL SELECT 'lamps',id::text,COALESCE(name,device_id),'lamp',deleted_at FROM lamps WHERE deleted_at IS NOT NULL
    ORDER BY deleted_at DESC`;
}
export async function changeTrash(resource, id, restore, user) {
  if (!tables.has(resource))
    throw createError({
      statusCode: 404,
      statusMessage: "Tipo de registro no encontrado",
    });
  return db.begin(async (sql) => {
    const [row] =
      await sql`SELECT * FROM ${sql(resource)} WHERE id::text=${id} FOR UPDATE`;
    if (!row)
      throw createError({
        statusCode: 404,
        statusMessage: "Registro no encontrado",
      });
    if (!!row.deleted_at === !restore) return { ok: true };
    if (!restore && ["lamps", "gateways"].includes(resource)) {
      const [active] =
        await sql`SELECT id FROM test_requests WHERE ${sql(resource === "lamps" ? "lamp_id" : "gateway_id")}::text=${id} AND status IN ('sent','accepted','unknown') AND deadline_at>now()`;
      if (active)
        throw createError({
          statusCode: 409,
          statusMessage:
            "Espere a que termine o venza la prueba individual antes de retirar este registro",
        });
      await sql`UPDATE test_requests SET status='cancelled',reason='Registro enviado a papelera' WHERE ${sql(resource === "lamps" ? "lamp_id" : "gateway_id")}::text=${id} AND status='pending'`;
    }
    if (restore) {
      await sql`UPDATE ${sql(resource)} SET deleted_at=NULL WHERE id::text=${id}`;
      if (resource === "gateways")
        await sql`UPDATE gateways SET online=false WHERE id=${id}`;
    } else {
      await sql`UPDATE ${sql(resource)} SET deleted_at=now() WHERE id::text=${id}`;
      if (resource === "gateways")
        await sql`UPDATE gateways SET online=false WHERE id=${id}`;
      if (resource === "lamps")
        await sql`UPDATE commands SET status='superseded',reason='Lámpara enviada a papelera' WHERE lamp_id::text=${id} AND status IN ('pending','sent','deferred')`;
    }
    await audit(sql, user, resource + (restore ? ".restore" : ".trash"), id, {
      name: row.name || row.device_id,
    });
    return { ok: true };
  });
}
