// Planificación de pruebas: valores permitidos y cálculo de la próxima ejecución.
// Compartido por la web (vista) y las pruebas; el worker calcula las ventanas en SQL.

/** Días ISO: 1 = lunes … 7 = domingo. */
export const WEEKDAYS = [
  { value: 1, short: "L", label: "Lunes" },
  { value: 2, short: "M", label: "Martes" },
  { value: 3, short: "X", label: "Miércoles" },
  { value: 4, short: "J", label: "Jueves" },
  { value: 5, short: "V", label: "Viernes" },
  { value: 6, short: "S", label: "Sábado" },
  { value: 7, short: "D", label: "Domingo" },
];

/** Duraciones ofrecidas (s). El gateway limita el máximo (test_max_duration_s). */
export const TEST_DURATIONS = [30, 60, 120, 180, 240, 300];

/** Minutos durante los que se reintenta una prueba programada. */
export const SCHEDULE_WINDOW_MIN = 15;

export const SCHEDULE_TIMEZONE = "America/Lima";

export function durationText(s) {
  return s < 60 ? `${s} s` : s % 60 ? `${Math.floor(s / 60)} min ${s % 60} s` : `${s / 60} min`;
}

/** "Lunes y jueves", "L, X, V, S" o "Todos los días" / "Lunes a viernes". */
export function daysText(days = []) {
  const set = [...new Set(days)].sort();
  if (set.length === 7) return "Todos los días";
  if (set.join() === "1,2,3,4,5") return "Lunes a viernes";
  if (set.join() === "6,7") return "Fines de semana";
  if (set.length <= 3) {
    const names = set.map((d, i) => {
      const n = WEEKDAYS[d - 1]?.label || "";
      return i ? n.toLowerCase() : n;
    });
    return names.length > 1
      ? names.slice(0, -1).join(", ") + " y " + names.at(-1)
      : names[0] || "";
  }
  return set.map((d) => WEEKDAYS[d - 1]?.short).join(", ");
}

// Partes de fecha/hora de un instante en una zona horaria.
function zoned(ms, timeZone) {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone,
    hourCycle: "h23",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    weekday: "short",
  }).formatToParts(new Date(ms));
  const get = (t) => parts.find((p) => p.type === t)?.value;
  const isoDay = { Mon: 1, Tue: 2, Wed: 3, Thu: 4, Fri: 5, Sat: 6, Sun: 7 }[get("weekday")];
  return {
    y: +get("year"),
    m: +get("month"),
    d: +get("day"),
    h: +get("hour"),
    mi: +get("minute"),
    s: +get("second"),
    isoDay,
  };
}

// Instante UTC de una fecha y hora locales de la zona (resuelve el desfase de la zona).
function localToInstant(y, m, d, h, mi, timeZone) {
  const guess = Date.UTC(y, m - 1, d, h, mi);
  const z = zoned(guess, timeZone);
  const offset = Date.UTC(z.y, z.m - 1, z.d, z.h, z.mi, z.s) - guess;
  return guess - offset;
}

/**
 * Próxima ejecución (Date) de una planificación, o null si está desactivada.
 * Si la ventana de reintento de hoy sigue abierta, devuelve esa misma hora
 * (con windowMin = 0 devuelve solo horas futuras).
 * @param {{enabled?: boolean, days: number[], time: string, timezone?: string}} schedule
 */
export function nextRun(schedule, now = new Date(), windowMin = SCHEDULE_WINDOW_MIN) {
  if (!schedule || schedule.enabled === false || !schedule.days?.length) return null;
  const tz = schedule.timezone || SCHEDULE_TIMEZONE;
  const [h, mi] = String(schedule.time).split(":").map(Number);
  const nowMs = +now;
  const today = zoned(nowMs, tz);
  for (let i = 0; i <= 7; i++) {
    const base = Date.UTC(today.y, today.m - 1, today.d + i);
    const day = new Date(base);
    const y = day.getUTCFullYear(),
      m = day.getUTCMonth() + 1,
      d = day.getUTCDate();
    const isoDay = ((today.isoDay - 1 + i) % 7) + 1;
    if (!schedule.days.includes(isoDay)) continue;
    const at = localToInstant(y, m, d, h, mi, tz);
    if (at + windowMin * 60000 > nowMs) return new Date(at);
  }
  return null;
}
