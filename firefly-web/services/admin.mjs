import { z } from "zod";
import { db } from "./lib/db.mjs";
import { hashPassword } from "./lib/security.mjs";
import { migrate } from "./migrate.mjs";
await migrate();
const email = (process.env.ADMIN_EMAIL || "").trim().toLowerCase(),
  password = process.env.ADMIN_PASSWORD || "";
if (
  !z.string().email().safeParse(email).success ||
  password.length < 12 ||
  password.length > 128
)
  throw new Error(
    "ADMIN_EMAIL y ADMIN_PASSWORD (12 caracteres mínimo) obligatorios",
  );
const result =
  await db`INSERT INTO users(email,name,password_hash,role) VALUES(${email},${process.env.ADMIN_NAME || "Administrador"},${hashPassword(password)},'admin') ON CONFLICT(email) DO NOTHING RETURNING id`;
console.log(
  result.length ? "Administrador creado." : "La cuenta existe; no se modificó.",
);
await db.end();
