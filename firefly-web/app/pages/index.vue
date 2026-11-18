<script setup lang="ts">
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
            pending: l.pending_assignment,
          } as any
        )[filters.state]),
  ),
);
const stats = computed(() => [
  { label: "Lámparas registradas", value: rows.value?.length || 0, color: "" },
  {
    label: "En línea",
    value: rows.value?.filter((l) => l.connected).length || 0,
    color: "green",
  },
  {
    label: "Con alertas",
    value: rows.value?.filter((l) => l.alert || l.critical).length || 0,
    color: "amber",
  },
  {
    label: "Sin comunicación",
    value: rows.value?.filter((l) => !l.connected).length || 0,
    color: "gray",
  },
]);
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
      <span class="pill" :class="live ? 'good' : 'neutral'"
        >●
        {{
          live ? "Actualización en vivo" : "Conectando actualizaciones"
        }}</span
      >
    </div>
    <div class="stats">
      <div v-for="s in stats" :key="s.label" class="stat">
        <span>{{ s.label }}</span><strong :class="s.color">{{
          s.value
        }}</strong>
      </div>
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
        <input
          v-model="search"
          aria-label="Buscar lámpara"
          class="search"
          placeholder="Buscar nombre, identificador o lugar…"
        />
      </div>
      <div class="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Lámpara</th>
              <th>Ubicación</th>
              <th>Estado</th>
              <th>Batería</th>
              <th>Última comunicación</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="l in filtered" :key="l.id">
              <td>
                <strong>{{ l.name || "Sin asignar" }}</strong><small
                  class="mono"
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
                {{ dateText(l.last_seen)
                }}<small v-if="l.age_s !== null"
                  >Hace {{ Math.max(0, Math.floor(l.age_s)) }} s</small
                >
              </td>
              <td>
                <NuxtLink :to="'/lamparas/' + l.id" class="text-link"
                  >Ver ficha →</NuxtLink
                >
              </td>
            </tr>
            <tr v-if="!filtered.length">
              <td colspan="6">
                <div class="empty">
                  <strong>{{
                    rows?.length ? "Sin coincidencias" : "Aún no hay lámparas"
                  }}</strong>
                  <p>
                    Registra un gateway y conecta sus dispositivos para comenzar
                    la supervisión.
                  </p>
                  <NuxtLink to="/admin" class="text-link"
                    >Administrar instalación →</NuxtLink
                  >
                </div>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
    <div class="notice">
      Las lecturas se conservan cuando un dispositivo pierde comunicación.
      Revisa siempre su fecha; el estado de alimentación no confirma por sí solo
      que la lámpara ilumina correctamente.
    </div>
  </section>
</template>
