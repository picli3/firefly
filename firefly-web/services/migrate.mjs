import { readFile, readdir } from "node:fs/promises";
import { db } from "./lib/db.mjs";
export async function migrate() {
  await db.begin(async (sql) => {
    await sql`SELECT pg_advisory_xact_lock(91471022)`;
    await sql`CREATE TABLE IF NOT EXISTS migrations(name text PRIMARY KEY, applied_at timestamptz DEFAULT now())`;
    for (const name of (
      await readdir(new URL("./migrations/", import.meta.url))
    )
      .filter((x) => x.endsWith(".sql"))
      .sort()) {
      if ((await sql`SELECT 1 FROM migrations WHERE name=${name}`).length)
        continue;
      await sql.unsafe(
        await readFile(
          new URL("./migrations/" + name, import.meta.url),
          "utf8",
        ),
      );
      await sql`INSERT INTO migrations(name) VALUES(${name})`;
      console.log("Migración aplicada:", name);
    }
  });
}
if (process.argv[1]?.endsWith("migrate.mjs")) {
  await migrate();
  await db.end();
}
