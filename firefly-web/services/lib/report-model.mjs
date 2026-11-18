const MONTHS = [
  "Enero",
  "Febrero",
  "Marzo",
  "Abril",
  "Mayo",
  "Junio",
  "Julio",
  "Agosto",
  "Septiembre",
  "Octubre",
  "Noviembre",
  "Diciembre",
];
const localParts = new Intl.DateTimeFormat("en-CA", {
  timeZone: "America/Lima",
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
});
export function reportDate(row) {
  return new Date(row.measured_at || row.received_at);
}
function monthKey(date) {
  const p = Object.fromEntries(
    localParts.formatToParts(date).map((x) => [x.type, x.value]),
  );
  return { year: Number(p.year), month: Number(p.month) - 1 };
}
function emptyBucket(year, month) {
  return {
    year,
    month,
    name: MONTHS[month],
    measurements: 0,
    valid: 0,
    voltageSum: 0,
    currentSum: 0,
    temperatureSum: 0,
    tests: 0,
    pass: 0,
  };
}
function average(sum, count) {
  return count ? sum / count : null;
}
function finish(bucket) {
  return {
    ...bucket,
    voltage: average(bucket.voltageSum, bucket.valid),
    current: average(bucket.currentSum, bucket.valid),
    temperature: average(bucket.temperatureSum, bucket.valid),
    passRate: average(bucket.pass * 100, bucket.tests),
  };
}
export function buildReportModel(
  measurements,
  tests,
  filters = {},
  context = {},
) {
  const generatedAt = new Date(context.generatedAt || Date.now());
  const from = new Date(filters.from || +generatedAt - 86400000),
    to = new Date(filters.to || generatedAt);
  if (!Number.isFinite(+from) || !Number.isFinite(+to) || from > to)
    throw Error("Rango de fechas inválido");
  const first = monthKey(from),
    last = monthKey(to);
  if (last.year - first.year > 11)
    throw Error("El PDF admite hasta 12 años; reduzca el rango de fechas");
  const buckets = new Map();
  const get = (y, m) => {
    const key = y + ":" + m;
    if (!buckets.has(key)) buckets.set(key, emptyBucket(y, m));
    return buckets.get(key);
  };
  const lampGroups = new Map();
  let invalid = 0,
    unknownProfiles = 0,
    receptionOnly = 0;
  for (const row of measurements) {
    const { year, month } = monthKey(reportDate(row)),
      bucket = get(year, month);
    bucket.measurements++;
    const d = row.data,
      valid =
        !!(d.flags & 8) &&
        [d.voltage_mv, d.current_ma, d.temperature_centi_c].every(
          Number.isFinite,
        );
    if (valid) {
      bucket.valid++;
      bucket.voltageSum += d.voltage_mv / 1000;
      bucket.currentSum += d.current_ma;
      bucket.temperatureSum += d.temperature_centi_c / 100;
    } else invalid++;
    if (!row.measured_at) receptionOnly++;
    if (!row.profile_snapshot) unknownProfiles++;
    const key = JSON.stringify([
      row.lamp_id,
      row.config_version ?? null,
      row.profile_snapshot?.name || null,
      row.location_snapshot,
    ]);
    const group = lampGroups.get(key) || {
      row,
      count: 0,
      valid: 0,
      min: null,
      max: null,
      alerts: 0,
    };
    group.count++;
    if (valid) {
      group.valid++;
      group.min =
        group.min === null ? d.voltage_mv : Math.min(group.min, d.voltage_mv);
      group.max =
        group.max === null ? d.voltage_mv : Math.max(group.max, d.voltage_mv);
    }
    if (d.flags & 6) group.alerts++;
    lampGroups.set(key, group);
  }
  for (const row of tests) {
    const { year, month } = monthKey(reportDate(row)),
      bucket = get(year, month);
    bucket.tests++;
    if (row.data.result === "PASS") bucket.pass++;
    if (!row.measured_at) receptionOnly++;
  }
  const years = [];
  for (let y = first.year; y <= last.year; y++) {
    const months = MONTHS.map((_, m) => {
      const b = finish(get(y, m)),
        previous = buckets.get(
          (m === 0 ? y - 1 : y) + ":" + (m === 0 ? 11 : m - 1),
        );
      const p = previous ? finish(previous).passRate : null;
      return {
        ...b,
        variation: b.passRate !== null && p !== null ? b.passRate - p : null,
      };
    });
    const total = months.reduce(
      (a, b) => {
        for (const key of [
          "measurements",
          "valid",
          "voltageSum",
          "currentSum",
          "temperatureSum",
          "tests",
          "pass",
        ])
          a[key] += b[key];
        return a;
      },
      emptyBucket(y, -1),
    );
    const rows = tests.filter((r) => monthKey(reportDate(r)).year === y),
      counts = { PASS: 0, LOW_VOLTAGE: 0, LOW_CURRENT: 0, OTHER: 0 };
    for (const r of rows)
      counts[r.data.result in counts ? r.data.result : "OTHER"]++;
    years.push({
      year: y,
      months,
      total: finish(total),
      categories: [
        ["PASS", "PASS informado"],
        ["LOW_VOLTAGE", "Voltaje bajo"],
        ["LOW_CURRENT", "Corriente baja"],
        ["OTHER", "Otros resultados"],
      ].map(([key, label]) => ({
        key,
        label,
        count: counts[key],
        pct: rows.length ? (counts[key] * 100) / rows.length : null,
      })),
    });
  }
  const total = years.reduce(
    (a, b) => {
      for (const key of [
        "measurements",
        "valid",
        "voltageSum",
        "currentSum",
        "temperatureSum",
        "tests",
        "pass",
      ])
        a[key] += b.total[key];
      return a;
    },
    emptyBucket(0, -1),
  );
  const observed = years
    .flatMap((y) => y.months)
    .filter((m) => m.passRate !== null);
  return {
    years,
    total: finish(total),
    from,
    to,
    generatedAt,
    filters,
    scope: context.scope || "Toda la instalación",
    preparedBy: context.preparedBy || "No indicado",
    period:
      first.year === last.year && first.month === last.month
        ? MONTHS[first.month] + " " + first.year
        : first.year === last.year
          ? String(first.year) + " (período filtrado)"
          : first.year + " - " + last.year,
    groups: [...lampGroups.values()],
    lampCount: new Set([...measurements, ...tests].map((r) => r.lamp_id)).size,
    invalid,
    unknownProfiles,
    receptionOnly,
    best: observed.length
      ? observed.reduce((a, b) => (b.passRate > a.passRate ? b : a))
      : null,
    worst: observed.length
      ? observed.reduce((a, b) => (b.passRate < a.passRate ? b : a))
      : null,
    tests,
  };
}
