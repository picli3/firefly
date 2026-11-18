<script setup lang="ts">
const props = defineProps<{
  rows: any[];
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

const samples = computed(() =>
  [...props.rows]
    .reverse()
    .filter((r) => r.data.flags & 8)
    .map((r) => ({
      v: r.data[props.field] / (props.divisor || 1),
      t: r.measured_at || r.received_at,
    })),
);
const values = computed(() => samples.value.map((s) => s.v));
const min = computed(() => Math.min(...values.value)),
  max = computed(() => Math.max(...values.value));

// "Nice" ticks: step of 1, 2 or 5 × 10^n, about 4 intervals.
const scale = computed(() => {
  let lo = min.value,
    hi = max.value;
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

const x = (i: number) =>
  L + (i * plotW) / Math.max(1, samples.value.length - 1);
const y = (v: number) =>
  T + plotH - ((v - scale.value.start) * plotH) / (scale.value.end - scale.value.start);

const points = computed(() =>
  values.value.map((v, i) => `${x(i)},${y(v)}`).join(" "),
);

const timeText = (t: any, withDate = false) =>
  t
    ? new Date(t).toLocaleString("es-PE", {
        timeZone: "America/Lima",
        ...(withDate ? { day: "2-digit", month: "2-digit" } : {}),
        hour: "2-digit",
        minute: "2-digit",
      })
    : "—";
const xTicks = computed(() => {
  const n = samples.value.length;
  if (!n) return [];
  const idx = n === 1 ? [0] : n === 2 ? [0, 1] : [0, Math.floor((n - 1) / 2), n - 1];
  const first = samples.value[0]?.t,
    last = samples.value[n - 1]?.t;
  const multiDay =
    first && last && new Date(first).toDateString() !== new Date(last).toDateString();
  return idx.map((i, k) => ({
    x: x(i),
    anchor: idx.length > 1 && k === 0 ? "start" : k === idx.length - 1 && idx.length > 1 ? "end" : "middle",
    text: timeText(samples.value[i]!.t, !!multiDay),
  }));
});

// Hover / keyboard readout.
const svg = ref<SVGSVGElement>();
const wrap = ref<HTMLElement>();
const active = ref<number | null>(null);
const tipPos = ref({ left: 0, top: 0 });

function place(i: number) {
  active.value = i;
  const el = svg.value,
    box = wrap.value;
  const m = el?.getScreenCTM();
  if (!el || !box || !m) return;
  const p = new DOMPoint(x(i), y(values.value[i]!)).matrixTransform(m);
  const r = box.getBoundingClientRect();
  tipPos.value = { left: p.x - r.left, top: p.y - r.top };
}
function onMove(e: PointerEvent) {
  const el = svg.value,
    m = el?.getScreenCTM();
  if (!el || !m || !samples.value.length) return;
  const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(m.inverse());
  const n = samples.value.length;
  const i = Math.round(((p.x - L) / plotW) * Math.max(1, n - 1));
  place(Math.min(n - 1, Math.max(0, i)));
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
const activeSample = computed(() =>
  active.value === null ? null : samples.value[active.value],
);
const tipSide = computed(() =>
  active.value !== null && x(active.value) > L + plotW * 0.6 ? "left" : "right",
);
</script>
<template>
  <div class="panel chart">
    <h3>{{ label }}</h3>
    <template v-if="values.length"
      ><div class="chart-range">
        {{ fmt(min) }} — {{ fmt(max) }} {{ unit }}
      </div>
      <div ref="wrap" class="chart-plot">
        <svg
          ref="svg"
          :viewBox="`0 0 ${W} ${H}`"
          role="img"
          :aria-label="`${label}, de ${fmt(min)} a ${fmt(max)} ${unit}. Use las flechas para recorrer las muestras.`"
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
          <polyline
            :points="points"
            fill="none"
            stroke="#16785c"
            stroke-width="2"
            stroke-linejoin="round"
            stroke-linecap="round"
          />
          <g v-if="active !== null" class="chart-cursor">
            <line :x1="x(active)" :x2="x(active)" :y1="T" :y2="T + plotH" />
            <circle :cx="x(active)" :cy="y(values[active]!)" r="4.5" />
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
          <span>{{ timeText(activeSample.t, true) }}</span>
        </div>
      </div>
      <small class="muted"
        >Muestras válidas recientes · orden cronológico de recepción · pase el
        cursor para ver cada valor</small
      ></template
    >
    <p v-else class="empty">Sin muestras válidas</p>
  </div>
</template>
