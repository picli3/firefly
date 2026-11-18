import {
  randomBytes,
  scryptSync,
  timingSafeEqual,
  createHash,
  pbkdf2Sync,
} from "node:crypto";
export const digest = (x) => createHash("sha256").update(x).digest("hex");
export function hashPassword(p) {
  const salt = randomBytes(16).toString("hex");
  return salt + ":" + scryptSync(p, salt, 64).toString("hex");
}
export function verifyPassword(p, h) {
  try {
    const [salt, hash] = h.split(":");
    const a = scryptSync(p, salt, 64);
    const b = Buffer.from(hash, "hex");
    return a.length === b.length && timingSafeEqual(a, b);
  } catch {
    return false;
  }
}
export function mqttHash(p) {
  const salt = randomBytes(12);
  return (
    "$7$210000$" +
    salt.toString("base64") +
    "$" +
    pbkdf2Sync(p, salt, 210000, 64, "sha512").toString("base64")
  );
}
export function newToken() {
  return randomBytes(32).toString("hex");
}
