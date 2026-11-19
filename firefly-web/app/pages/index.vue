<script setup lang="ts">
import { daysText, durationText, nextRun } from "#shared/schedule.mjs";
// Texto del indicador de pruebas programadas de una lámpara.
function scheduleText(l: any) {
  const base = `${daysText(l.schedule_days)} a las ${l.schedule_time} · ${durationText(l.schedule_duration_s)}`;
  const next = nextRun(
    { days: l.schedule_days, time: l.schedule_time, timezone: l.schedule_timezone },
    new Date(),
    0,
  );
  return next ? `Pruebas programadas: ${base}. Próxima: ${dateText(next)}` : `Pruebas programadas: ${base}`;
}
const fetcher = useRequestFetch();
const { data: rows, refresh } = await useAsyncData<any[]>("lamps", () =>
  fetcher("/api/lamps"),
);
const { data: locations } = await useAsyncData<any[]>("locations", () =>
  fetcher("/api/locations"),
);
const { data: gateways } = await useAsyncData<any[]>("gateways", () =>
  fetcher("/api/gateways"),
);
const filters = reactive({
    site: "",
    building: "",
    level: "",
    gateway: "",
    state: "",
  }),
  search = ref(""),
  live = ref(false);
const filtered = computed(() =>
  (rows.value || []).filter(
    (l) =>
      (!filters.site || l.site_id === filters.site) &&
      (!filters.building || l.building_id === filters.building) &&
      (!filters.level || l.level_id === filters.level) &&
      (!filters.gateway || l.gateway_id === filters.gateway) &&
      (!search.value ||
        [l.name, l.device_id, l.place].some((v) =>
          v?.toLowerCase().includes(search.value.toLowerCase()),
        )) &&
      (!filters.state ||
        (
          {
            alert: l.alert || l.critical,
            battery: !l.ac && !!l.latest,
            testing: l.testing,
            offline: !l.connected,
            online: l.connected,
            scheduled: !!l.schedule_enabled,
            pending: l.pending_assignment,
          } as any
        )[filters.state]),
  ),
);
// Indicadores: al pulsar uno se filtra la tabla por ese estado.
const stats = computed(() => [
  {
    label: "Lámparas registradas",
    value: rows.value?.length || 0,
    color: "",
    icon: "i-lucide-lamp",
    state: "",
  },
  {
    label: "En línea",
    value: rows.value?.filter((l) => l.connected).length || 0,
    color: "green",
    icon: "i-lucide-wifi",
    state: "online",
  },
  {
    label: "Con alertas",
    value: rows.value?.filter((l) => l.alert || l.critical).length || 0,
    color: "amber",
    icon: "i-lucide-triangle-alert",
    state: "alert",
  },
  {
    label: "Sin comunicación",
    value: rows.value?.filter((l) => !l.connected).length || 0,
    color: "gray",
    icon: "i-lucide-wifi-off",
    state: "offline",
  },
]);
const filtersActive = computed(
  () => Object.values(filters).some(Boolean) || !!search.value,
);
function clearFilters() {
  Object.assign(filters, { site: "", building: "", level: "", gateway: "", state: "" });
  search.value = "";
}
// Orden de la tabla (clic en la cabecera). Por defecto, lo que requiere atención primero.
type SortKey = "name" | "level" | "state" | "voltage" | "seen";
const sort = reactive<{ key: SortKey; dir: 1 | -1 }>({ key: "state", dir: 1 });
function setSort(key: SortKey) {
  if (sort.key === key) sort.dir = sort.dir === 1 ? -1 : 1;
  else Object.assign(sort, { key, dir: 1 });
}
const severity = (l: any) =>
  l.critical ? 0 : l.alert ? 1 : !l.connected ? 2 : !l.ac && l.latest ? 3 : 4;
const sortKeys: Record<SortKey, (l: any) => string | number> = {
  name: (l) => (l.name || l.device_id || "").toLowerCase(),
  level: (l) => `${l.level || "~"} ${l.place || ""}`.toLowerCase(),
  state: (l) => severity(l),
  voltage: (l) => l.latest?.voltage_mv ?? -1,
  seen: (l) => l.age_s ?? Number.MAX_SAFE_INTEGER,
};
const sorted = computed(() => {
  const f = sortKeys[sort.key];
  return [...filtered.value].sort((a, b) => {
    const x = f(a),
      y = f(b);
    return (x < y ? -1 : x > y ? 1 : 0) * sort.dir;
  });
});
const sortIcon = (k: SortKey) =>
  sort.key !== k
    ? "i-lucide-arrow-up-down"
    : sort.dir === 1
      ? "i-lucide-arrow-up"
      : "i-lucide-arrow-down";
const ariaSort = (k: SortKey) =>
  sort.key !== k ? "none" : sort.dir === 1 ? "ascending" : "descending";
let events: EventSource | undefined;
onMounted(() => {
  events = new EventSource("/api/events");
  events.addEventListener("snapshot", (e) => {
    rows.value = JSON.parse((e as MessageEvent).data);
    live.value = true;
  });
  events.onerror = () => {
    live.value = false;
  };
});
onBeforeUnmount(() => events?.close());
</script>
<template>
  <section>
    <div class="page-heading">
      <div>
        <p class="eyebrow">ESTADO DE LA INSTALACIÓN</p>
        <h1>Vista general</h1>
        <p class="muted">
          Cada luminaria, su ubicación y su última información recibida.
        </p>
      </div>
      <UBadge
        :color="live ? 'success' : 'neutral'"
        variant="subtle"
        :icon="live ? 'i-lucide-radio' : 'i-lucide-loader-circle'"
        :label="live ? 'Actualización en vivo' : 'Conectando actualizaciones'"
      />
    </div>
    <div class="stats">
      <button
        v-for="s in stats"
        :key="s.label"
        type="button"
        class="stat stat-button"
        :class="{ active: s.state && filters.state === s.state }"
        :aria-pressed="!!s.state && filters.state === s.state"
        @click="filters.state = filters.state === s.state ? '' : s.state"
      >
        <span class="stat-label"><UIcon :name="s.icon" />{{ s.label }}</span
        ><strong :class="s.color">{{ s.value }}</strong>
      </button>
    </div>
    <LocationFilters
      v-model="filters"
      :locations="locations || []"
      :gateways="gateways || []"
    />
    <section class="panel">
      <div class="panel-heading">
        <h2>
          Luminarias <span class="muted">{{ filtered.length }}</span>
        </h2>
        <div class="panel-tools">
          <UButton
            v-if="filtersActive"
            color="neutral"
            variant="ghost"
            size="sm"
            icon="i-lucide-filter-x"
            label="Limpiar filtros"
            @click="clearFilters"
          />
          <UInput
            v-model="search"
            icon="i-lucide-search"
            aria-label="Buscar lámpara"
            placeholder="Buscar nombre, identificador o lugar…"
            class="search-input"
          />
        </div>
      </div>
      <!-- Escritorio y tablet: tabla ordenable -->
      <div class="table-scroll overview-table">
        <table>
          <thead>
            <tr>
              <th
                v-for="c in [
                  ['name', 'Lámpara'],
                  ['level', 'Ubicación'],
                  ['state', 'Estado'],
                  ['voltage', 'Batería'],
                  ['seen', 'Última comunicación'],
                ] as const"
                :key="c[0]"
                :aria-sort="ariaSort(c[0])"
              >
                <button type="button" class="th-sort" @click="setSort(c[0])">
                  {{ c[1] }} <UIcon :name="sortIcon(c[0])" />
                </button>
              </th>
              <th><span class="sr-only">Acciones</span></th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="l in sorted"
              :key="l.id"
              class="row-link"
              @click="navigateTo('/lamparas/' + l.id)"
            >
              <td>
                <div class="lamp-name">
                  <strong>{{ l.name || "Sin asignar" }}</strong>
                  <UTooltip v-if="l.schedule_enabled" :text="scheduleText(l)">
                    <span class="schedule-chip" :aria-label="scheduleText(l)" role="img"
                      ><UIcon name="i-lucide-calendar-clock"
                    /></span>
                  </UTooltip>
                </div>
                <small class="mono"
                  >{{ l.device_id }} · {{ l.gateway_name }}</small
                >
              </td>
              <td>
                {{ l.level || "Sin nivel"
                }}<small>{{
                  l.place || l.building || "Ubicación pendiente"
                }}</small>
              </td>
              <td><StatusPills :lamp="l" /></td>
              <td>
                {{
                  l.latest
                    ? (l.latest.voltage_mv / 1000).toFixed(3) + " V"
                    : "—"
                }}<small>{{
                  l.latest?.flags & 8
                    ? (l.latest.temperature_centi_c / 100).toFixed(1) + " °C"
                    : "Sin lectura válida"
                }}</small>
              </td>
              <td>
                {{ dateText(l.last_seen) }}<small>{{ agoText(l.age_s) }}</small>
              </td>
              <td>
                <UButton
                  :to="'/lamparas/' + l.id"
                  variant="ghost"
                  size="sm"
                  trailing-icon="i-lucide-chevron-right"
                  label="Ver ficha"
                  @click.stop
                />
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <!-- Móvil: tarjetas -->
      <ul class="overview-cards">
        <li v-for="l in sorted" :key="l.id">
          <NuxtLink :to="'/lamparas/' + l.id" class="lamp-card">
            <div class="lamp-card-head">
              <strong>{{ l.name || "Sin asignar" }}</strong>
              <UIcon name="i-lucide-chevron-right" />
            </div>
            <small v-if="l.schedule_enabled" class="schedule-line"
              ><UIcon name="i-lucide-calendar-clock" />{{
                `${daysText(l.schedule_days)} · ${l.schedule_time} · ${durationText(l.schedule_duration_s)}`
              }}</small
            >
            <small class="muted"
              >{{ l.level || "Sin nivel" }} ·
              {{ l.place || l.building || "Ubicación pendiente" }}</small
            >
            <StatusPills :lamp="l" />
            <div class="lamp-card-data">
              <span
                ><UIcon name="i-lucide-battery-medium" />{{
                  l.latest ? (l.latest.voltage_mv / 1000).toFixed(3) + " V" : "—"
                }}</span
              >
              <span
                ><UIcon name="i-lucide-thermometer" />{{
                  l.latest?.flags & 8
                    ? (l.latest.temperature_centi_c / 100).toFixed(1) + " °C"
                    : "—"
                }}</span
              >
              <span><UIcon name="i-lucide-clock" />{{ agoText(l.age_s) || "Sin datos" }}</span>
            </div>
          </NuxtLink>
        </li>
      </ul>
      <div v-if="!filtered.length" class="empty">
        <strong>{{
          rows?.length ? "Sin coincidencias" : "Aún no hay lámparas"
        }}</strong>
        <p v-if="rows?.length">Ninguna lámpara cumple los filtros actuales.</p>
        <p v-else>
          Registra un gateway y conecta sus dispositivos para comenzar la
          supervisión.
        </p>
        <UButton
          v-if="rows?.length"
          color="neutral"
          variant="outline"
          icon="i-lucide-filter-x"
          label="Limpiar filtros"
          @click="clearFilters"
        />
        <UButton
          v-else
          to="/admin"
          variant="ghost"
          trailing-icon="i-lucide-arrow-right"
          label="Administrar instalación"
        />
      </div>
    </section>
    <UAlert
      color="neutral"
      variant="subtle"
      icon="i-lucide-info"
      description="Las lecturas se conservan cuando un dispositivo pierde comunicación. Revisa siempre su fecha; el estado de alimentación no confirma por sí solo que la lámpara ilumina correctamente."
    />
  </section>
</template>
