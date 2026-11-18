<script setup lang="ts">
// Series comes from GET /api/lamps/:id/series: points aggregated per bucket
// (avg/min/max of valid-sensor samples), plus the window length and server time.
const props = defineProps<{
  series: {
    range: string;
    seconds: number;
    bucket: number;
    now: string;
    points: any[];
  } | null;
  field: string;
  label: string;
  divisor?: number;
  unit: string;
}>();

// Plot geometry in viewBox units.
const W = 600,
  H = 200,
  L = 56,
  R = 14,
  T = 12,
  B = 30;
const plotW = W - L - R,
  plotH = H - T - B;

const div = computed(() => props.divisor || 1);
const bucketMs = computed(() => (props.series?.bucket || 60) * 1000);
const samples = computed(() =>
  (props.series?.points || [])
    .filter((p) => p[props.field] !== null)
    .map((p) => ({
      start: Date.parse(p.t),
      t: Date.parse(p.t) + bucketMs.value / 2,
      v: p[props.field] / div.value,
      lo: p[props.field + "_min"] / div.value,
      hi: p[props.field + "_max"] / div.value,
      n: p.n as number,
    })),
);
const domain = computed(() => {
  const end = props.series ? Date.parse(props.series.now) : Date.now();
  return { start: end - (props.series?.seconds || 86400) * 1000, end };
});
const min = computed(() => Math.min(...samples.value.map((s) => s.lo))),
  max = computed(() => Math.max(...samples.value.map((s) => s.hi)));

// "Nice" Y ticks: step of 1, 2 or 5 × 10^n, about 4 intervals.
const scale = computed(() => {
  const avgs = samples.value.map((s) => s.v);
  let lo = Math.min(...avgs),
    hi = Math.max(...avgs);
  if (hi - lo < 1e-9) {
    const pad = Math.abs(hi) * 0.05 || 1;
    lo -= pad;
    hi += pad;
  }
  const raw = (hi - lo) / 4;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 5, 10].map((m) => m * mag).find((s) => s >= raw)!;
  const start = Math.floor(lo / step) * step,
    end = Math.ceil(hi / step) * step;
  const ticks: number[] = [];
  const count = Math.round((end - start) / step);
  for (let k = 0; k <= count; k++) ticks.push(start + k * step);
  const decimals = Math.max(0, -Math.floor(Math.log10(step) + 1e-9));
  return { start, end, ticks, decimals };
});
const valueDecimals = computed(() => Math.min(3, scale.value.decimals + 1));
const fmt = (v: number, d = valueDecimals.value) =>
  v.toLocaleString("es-PE", { minimumFractionDigits: d, maximumFractionDigits: d });

const x = (t: number) =>
  L + ((t - domain.value.start) * plotW) / (domain.value.end - domain.value.start);
const y = (v: number) =>
  T + plotH - ((v - scale.value.start) * plotH) / (scale.value.end - scale.value.start);

// Break the line where no data arrived (lamp offline, gateway down...).
const gapMs = computed(() => Math.max(3 * bucketMs.value, 180_000));
const segments = computed(() => {
  const out: { d: string; single: boolean; i: number }[] = [];
  let cur: string[] = [],
    first = 0;
  samples.value.forEach((s, i) => {
    const prev = samples.value[i - 1];
    if (prev && s.t - prev.t > gapMs.value) {
      out.push({ d: "M" + cur.join("L"), single: cur.length === 1, i: first });
      cur = [];
    }
    if (!cur.length) first = i;
    cur.push(`${x(s.t).toFixed(1)},${y(s.v).toFixed(1)}`);
  });
  if (cur.length) out.push({ d: "M" + cur.join("L"), single: cur.length === 1, i: first });
  return out;
});

const longRange = computed(() => (props.series?.seconds || 0) > 86400);
const timeText = (t: number, mode: "axis" | "tip") => {
  const o: Intl.DateTimeFormatOptions = { timeZone: "America/Lima" };
  const days = (props.series?.seconds || 0) / 86400;
  if (mode === "axis") {
    if (days > 120) Object.assign(o, { month: "short", year: "2-digit" });
    else if (days > 1) Object.assign(o, { day: "2-digit", month: "2-digit" });
    else Object.assign(o, { hour: "2-digit", minute: "2-digit" });
  } else {
    Object.assign(o, { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" });
    if (days >= 365) {
      delete o.hour;
      delete o.minute;
      o.year = "numeric";
    }
  }
  return new Date(t).toLocaleString("es-PE", o);
};
const xTicks = computed(() =>
  [0, 0.25, 0.5, 0.75, 1].map((f, k, a) => {
    const t = domain.value.start + f * (domain.value.end - domain.value.start);
    return {
      x: x(t),
      anchor: k === 0 ? "start" : k === a.length - 1 ? "end" : "middle",
      text: timeText(t, "axis"),
    };
  }),
);

// Hover / keyboard readout.
const svg = ref<SVGSVGElement>();
const wrap = ref<HTMLElement>();
const active = ref<number | null>(null);
const tipPos = ref({ left: 0, top: 0 });

function place(i: number) {
  active.value = i;
  const el = svg.value,
    box = wrap.value,
    s = samples.value[i];
  const m = el?.getScreenCTM();
  if (!el || !box || !m || !s) return;
  const p = new DOMPoint(x(s.t), y(s.v)).matrixTransform(m);
  const r = box.getBoundingClientRect();
  tipPos.value = { left: p.x - r.left, top: p.y - r.top };
}
function onMove(e: PointerEvent) {
  const el = svg.value,
    m = el?.getScreenCTM();
  if (!el || !m || !samples.value.length) return;
  const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(m.inverse());
  const t =
    domain.value.start + ((p.x - L) / plotW) * (domain.value.end - domain.value.start);
  let best = 0;
  samples.value.forEach((s, i) => {
    if (Math.abs(s.t - t) < Math.abs(samples.value[best]!.t - t)) best = i;
  });
  place(best);
}
function onKey(e: KeyboardEvent) {
  const n = samples.value.length;
  if (!n) return;
  const cur = active.value ?? n - 1;
  const next =
    e.key === "ArrowLeft" ? cur - 1
    : e.key === "ArrowRight" ? cur + 1
    : e.key === "Home" ? 0
    : e.key === "End" ? n - 1
    : null;
  if (next === null) return;
  e.preventDefault();
  place(Math.min(n - 1, Math.max(0, next)));
}
watch(samples, () => {
  if (active.value !== null && active.value >= samples.value.length) active.value = null;
});
const activeSample = computed(() =>
  active.value === null ? null : samples.value[active.value],
);
const tipSide = computed(() =>
  activeSample.value && x(activeSample.value.t) > L + plotW * 0.6 ? "left" : "right",
);
</script>
<template>
  <div class="panel chart">
    <h3>{{ label }}</h3>
    <template v-if="samples.length"
      ><div class="chart-range">
        {{ fmt(min) }} — {{ fmt(max) }} {{ unit }}
      </div>
      <div ref="wrap" class="chart-plot">
        <svg
          ref="svg"
          :viewBox="`0 0 ${W} ${H}`"
          role="img"
          :aria-label="`${label}, de ${fmt(min)} a ${fmt(max)} ${unit}. Use las flechas para recorrer los puntos.`"
          tabindex="0"
          @pointermove="onMove"
          @pointerleave="active = null"
          @focus="place(samples.length - 1)"
          @blur="active = null"
          @keydown="onKey"
        >
          <g class="chart-grid">
            <line
              v-for="t in scale.ticks"
              :key="t"
              :x1="L"
              :x2="W - R"
              :y1="y(t)"
              :y2="y(t)"
            />
          </g>
          <g class="chart-axis">
            <text
              v-for="t in scale.ticks"
              :key="t"
              :x="L - 8"
              :y="y(t)"
              text-anchor="end"
              dominant-baseline="middle"
            >
              {{ fmt(t, scale.decimals) }}
            </text>
            <text :x="L - 8" :y="T - 2" text-anchor="end" class="chart-unit">
              {{ unit }}
            </text>
            <text
              v-for="(t, k) in xTicks"
              :key="k"
              :x="t.x"
              :y="H - 8"
              :text-anchor="t.anchor"
            >
              {{ t.text }}
            </text>
          </g>
          <template v-for="s in segments" :key="s.i">
            <circle
              v-if="s.single"
              class="chart-dot"
              :cx="x(samples[s.i]!.t)"
              :cy="y(samples[s.i]!.v)"
              r="2.5"
            />
            <path
              v-else
              :d="s.d"
              fill="none"
              stroke="var(--ff-chart-line)"
              stroke-width="2"
              stroke-linejoin="round"
              stroke-linecap="round"
            />
          </template>
          <g v-if="activeSample" class="chart-cursor">
            <line
              :x1="x(activeSample.t)"
              :x2="x(activeSample.t)"
              :y1="T"
              :y2="T + plotH"
            />
            <circle :cx="x(activeSample.t)" :cy="y(activeSample.v)" r="4.5" />
          </g>
          <rect
            :x="L"
            :y="T"
            :width="plotW"
            :height="plotH"
            fill="transparent"
          />
        </svg>
        <div
          v-if="activeSample"
          class="chart-tip"
          :class="tipSide"
          :style="{ left: `${tipPos.left}px`, top: `${tipPos.top}px` }"
          aria-live="polite"
        >
          <strong>{{ fmt(activeSample.v) }} {{ unit }}</strong>
          <span v-if="activeSample.n > 1"
            >mín {{ fmt(activeSample.lo) }} · máx {{ fmt(activeSample.hi) }} ·
            {{ activeSample.n }} muestras</span
          >
          <span>{{ timeText(activeSample.start, "tip") }}</span>
        </div>
      </div>
      <small class="muted"
        >{{
          longRange || (series?.bucket || 0) > 60
            ? "Promedio por intervalo de muestras válidas"
            : "Muestras válidas"
        }}
        · pase el cursor para ver cada valor</small
      ></template
    >
    <p v-else class="empty">Sin muestras válidas en este periodo</p>
  </div>
</template>
