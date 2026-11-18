<script setup lang="ts">
const props = defineProps<{
  rows: any[];
  field: string;
  label: string;
  divisor?: number;
  unit: string;
}>();
const values = computed(() =>
  [...props.rows]
    .reverse()
    .filter((r) => r.data.flags & 8)
    .map((r) => r.data[props.field] / (props.divisor || 1)),
);
const min = computed(() => Math.min(...values.value)),
  max = computed(() => Math.max(...values.value));
const points = computed(() =>
  values.value
    .map(
      (v, i) =>
        `${20 + (i * 560) / Math.max(1, values.value.length - 1)},${140 - ((v - min.value) * 110) / Math.max(0.01, max.value - min.value)}`,
    )
    .join(" "),
);
</script>
<template>
  <div class="panel chart">
    <h3>{{ label }}</h3>
    <template v-if="values.length"
      ><div class="chart-range">
        {{ min.toFixed(2) }} — {{ max.toFixed(2) }} {{ unit }}
      </div>
      <svg viewBox="0 0 600 170" role="img" :aria-label="label">
        <path
          d="M20 30H580 M20 85H580 M20 140H580"
          stroke="#e4ebe7"
          fill="none"
        />
        <polyline
          :points="points"
          fill="none"
          stroke="#16785c"
          stroke-width="2.5"
        /></svg
      ><small class="muted"
        >Muestras válidas recientes · orden cronológico de recepción</small
      ></template
    >
    <p v-else class="empty">Sin muestras válidas</p>
  </div>
</template>
