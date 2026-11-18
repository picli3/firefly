<script setup lang="ts">
const fetcher = useRequestFetch(),
  route = useRoute();
const { data: locations } = await useAsyncData<any[]>("locations", () =>
  fetcher("/api/locations"),
);
const { data: gateways } = await useAsyncData<any[]>("gateways", () =>
  fetcher("/api/gateways"),
);
const { data: lamps } = await useAsyncData<any[]>("report-lamps", () =>
  fetcher("/api/lamps"),
);
const localInput = (date: Date) =>
  new Date(+date - 5 * 3600000).toISOString().slice(0, 16);
const f = reactive({
  site: "",
  building: "",
  level: "",
  gateway: "",
  state: "",
  lamp: String(route.query.lamp || ""),
  from: localInput(new Date(Date.now() - 86400000)),
  to: localInput(new Date()),
});
const preview = ref<any>(null),
  error = ref(""),
  busy = ref(false),
  appliedQuery = ref("");
function query() {
  const entries = Object.entries(f)
    .filter(([, v]) => v)
    .map(([k, v]) => [
      k,
      ["from", "to"].includes(k) ? new Date(v + ":00-05:00").toISOString() : v,
    ]);
  return new URLSearchParams(entries).toString();
}
async function load() {
  error.value = "";
  busy.value = true;
  try {
    const q = query();
    preview.value = await $fetch("/api/reports?" + q);
    appliedQuery.value = q;
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
const stale = computed(() => {
  try {
    return appliedQuery.value !== query();
  } catch {
    return true;
  }
});
</script>
<template>
  <section>
    <div class="page-heading">
      <div>
        <p class="eyebrow">HISTÓRICOS Y EVIDENCIA</p>
        <h1>Reportes</h1>
        <p class="muted">
          Selecciona el alcance, revisa los datos y descarga el informe.
        </p>
      </div>
    </div>
    <form class="panel form-panel" @submit.prevent="load">
      <LocationFilters
        v-model="f"
        :locations="locations || []"
        :gateways="gateways || []"
        historical
      />
      <div class="filter-bar">
        <label
          >Desde · America/Lima<input
            v-model="f.from"
            type="datetime-local"
            required /></label
        ><label
          >Hasta · America/Lima<input
            v-model="f.to"
            type="datetime-local"
            required /></label
        ><label
          >Lámpara<FfSelect
            v-model="f.lamp"
            empty-label="Todas las lámparas"
            :items="(lamps || []).map((l: any) => ({ value: l.id, label: l.name || l.device_id }))"
          /></label
        >
        <UButton
          type="submit"
          icon="i-lucide-search"
          :loading="busy"
          :label="busy ? 'Consultando…' : 'Consultar reporte'"
          class="filter-submit"
        />
      </div>
    </form>
    <UAlert
      v-if="error"
      class="page-alert"
      color="error"
      variant="subtle"
      icon="i-lucide-circle-x"
      :description="error"
      role="alert"
    />
    <section v-if="preview" class="panel">
      <div class="panel-heading">
        <h2>
          {{ preview.measurement_count }} mediciones ·
          {{ preview.test_count }} resultados
        </h2>
        <div v-if="!stale" class="actions">
          <UButton
            :href="'/api/reports/report.pdf?' + appliedQuery"
            external
            icon="i-lucide-file-down"
            label="PDF mensual"
          />
          <UButton
            :href="'/api/reports/measurements.csv?' + appliedQuery"
            external
            color="neutral"
            variant="outline"
            icon="i-lucide-file-spreadsheet"
            label="CSV mediciones"
          />
          <UButton
            :href="'/api/reports/tests.csv?' + appliedQuery"
            external
            color="neutral"
            variant="outline"
            icon="i-lucide-file-spreadsheet"
            label="CSV pruebas"
          />
        </div>
        <UBadge
          v-else
          color="warning"
          variant="subtle"
          icon="i-lucide-refresh-cw"
          label="Filtros modificados: vuelve a consultar"
        />
      </div>
      <UAlert
        class="panel-alert"
        color="neutral"
        variant="subtle"
        icon="i-lucide-info"
        :title="`Vista previa: hasta ${preview.preview_limit} filas por tipo`"
        description="Las descargas incluyen todas las filas del filtro, hasta 50.000 por tipo; reduce el rango si se supera. Los datos sin hora de medición se filtran por recepción. El PDF sigue la plantilla mensual: los cuadros usan únicamente el rango y los filtros seleccionados; no completan meses sin datos."
      />
      <div class="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Lámpara</th>
              <th>Fecha / origen</th>
              <th>Ubicación histórica</th>
              <th>Voltaje</th>
              <th>Configuración</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="r in preview.measurements" :key="r.id">
              <td>
                {{ r.lampara }}<small>{{ r.gateway }}</small>
              </td>
              <td>
                {{ dateText(r.fecha_medicion || r.fecha_recepcion)
                }}<small>{{ r.origen_fecha }}</small>
              </td>
              <td>
                {{
                  [r.sede, r.edificio, r.nivel, r.lugar]
                    .filter(Boolean)
                    .join(" / ") || "Sin asignación"
                }}
              </td>
              <td>{{ r.voltage_mv / 1000 }} V</td>
              <td>{{ r.config_version ?? "Desconocida" }} · {{ r.perfil }}</td>
            </tr>
            <tr v-if="!preview.measurement_count">
              <td colspan="5" class="empty">Sin muestras en este periodo.</td>
            </tr>
          </tbody>
        </table>
        <h3 class="table-section-title">Resultados de pruebas</h3>
        <table>
          <thead>
            <tr>
              <th>Lámpara</th>
              <th>Fecha</th>
              <th>Resultado original</th>
              <th>Duración</th>
              <th>Perfil</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="r in preview.tests" :key="r.id">
              <td>{{ r.lampara }}</td>
              <td>
                {{ dateText(r.fecha_medicion || r.fecha_recepcion)
                }}<small>{{ r.origen_fecha }}</small>
              </td>
              <td>
                <UBadge
                  :color="r.result === 'PASS' ? 'success' : 'error'"
                  variant="subtle"
                  :label="r.result"
                />
              </td>
              <td>{{ r.duration_s }} s</td>
              <td>{{ r.perfil }} · {{ r.config_version ?? "Desconocida" }}</td>
            </tr>
            <tr v-if="!preview.test_count">
              <td colspan="5" class="empty">
                No se recibieron resultados; no equivale a una prueba aprobada.
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
    <div v-else class="panel empty">
      <UIcon name="i-lucide-file-search" class="empty-icon" />
      <strong>Define el periodo del informe</strong>
      <p>
        Los reportes conservan la ubicación y el perfil conocidos en cada
        registro.
      </p>
    </div>
  </section>
</template>
