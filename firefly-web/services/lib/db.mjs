import postgres from "postgres";
if (!process.env.DATABASE_URL) throw new Error("DATABASE_URL obligatorio");
export const db = postgres(process.env.DATABASE_URL, {
  max: 8,
  idle_timeout: 20,
  onnotice: () => {},
});
export async function audit(sql, user, action, target, data = {}) {
  await sql`INSERT INTO audits(user_id,action,target,data) VALUES(${user?.id || null},${action},${String(target)},${sql.json(data)})`;
}
export async function locationSnapshot(sql, lamp) {
  if (!lamp.location_id)
    return { place: lamp.place, level: null, building: null, site: null };
  const rows =
    await sql`SELECT l.name level,b.name building,s.name site,l.id level_id,b.id building_id,s.id site_id FROM locations l LEFT JOIN locations b ON b.id=l.parent_id LEFT JOIN locations s ON s.id=b.parent_id WHERE l.id=${lamp.location_id}`;
  return { ...rows[0], place: lamp.place };
}
