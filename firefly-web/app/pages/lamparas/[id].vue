<script setup lang="ts">
const route = useRoute(),
  fetcher = useRequestFetch(),
  { admin } = useSession();
const { data, refresh } = await useAsyncData<any>(
  "lamp-" + route.params.id,
  () => fetcher("/api/lamps/" + route.params.id),
);
const ranges = [
  ["1h", "Última hora"],
  ["24h", "24 horas"],
  ["7d", "7 días"],
  ["28d", "28 días"],
  ["3m", "3 meses"],
  ["1y", "1 año"],
] as const;
const range = ref<string>("24h");
const { data: series, refresh: refreshSeries } = await useAsyncData<any>(
  "lamp-series-" + route.params.id,
  () => fetcher(`/api/lamps/${route.params.id}/series?range=${range.value}`),
  { watch: [range] },
);
const { data: locations } = await useAsyncData<any[]>("locations", () =>
  fetcher("/api/locations"),
);
const { data: profiles } = await useAsyncData<any[]>("profiles", () =>
  fetcher("/api/profiles"),
);
const edit = reactive({
    name: "",
    model: "",
    nominal_mv: 6000,
    location_id: "",
    place: "",
  }),
  profile = ref(""),
  error = ref(""),
  message = ref(""),
  busy = ref(false);
watch(
  () => data.value?.lamp.id,
  () => {
    if (data.value)
      Object.assign(edit, {
        name: data.value.lamp.name || "",
        model: data.value.lamp.model,
        nominal_mv: data.value.lamp.nominal_mv || 6000,
        location_id: data.value.lamp.location_id || "",
        place: data.value.lamp.place,
      });
  },
  { immediate: true },
);
async function save() {
  busy.value = true;
  error.value = "";
  try {
    await $fetch("/api/lamps/" + route.params.id, {
      method: "PATCH",
      body: {
        ...edit,
        location_id: edit.location_id || null,
        nominal_mv: Number(edit.nominal_mv),
      },
    });
    await refresh();
    message.value = "Lámpara actualizada";
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
const confirmTest = ref(false),
  testRequestId = ref<string | null>(null);
const testState: Record<string, string> = {
  pending: "Pendiente de envío",
  sent: "Enviada · esperando gateway",
  accepted: "Gateway confirmó el envío a la lámpara",
  completed: "Resultado recibido",
  rejected: "Rechazada por el gateway",
  unknown: "Sin resultado confirmado",
  expired: "Venció sin enviarse",
  cancelled: "Cancelada",
};
const activeTest = computed(() =>
  data.value?.test_requests?.find(
    (r: any) =>
      ["pending", "sent", "accepted"].includes(r.status) ||
      (r.status === "unknown" &&
        new Date(r.deadline_at).getTime() > Date.now()),
  ),
);
async function runTest() {
  busy.value = true;
  error.value = "";
  message.value = "";
  testRequestId.value ||= crypto.randomUUID();
  try {
    await $fetch("/api/lamps/" + route.params.id + "/test", {
      method: "POST",
      body: { request_id: testRequestId.value },
    });
    testRequestId.value = null;
    confirmTest.value = false;
    await refresh();
    message.value =
      "Prueba individual solicitada. Consulta la confirmación y el resultado debajo.";
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
async function configure() {
  busy.value = true;
  error.value = "";
  try {
    await $fetch("/api/lamps/" + route.params.id + "/configure", {
      method: "POST",
      body: { profile_version_id: profile.value },
    });
    await refresh();
    message.value =
      "Configuración solicitada. Esperando confirmación de la lámpara.";
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
let timer: ReturnType<typeof setInterval>;
onMounted(() => {
  let tick = 0;
  timer = setInterval(() => {
    refresh();
    // Short windows follow live data; long ones only need a slow refresh.
    const every = range.value === "1h" ? 1 : range.value === "24h" ? 6 : 60;
    if (++tick % every === 0) refreshSeries();
  }, 5000);
});
onBeforeUnmount(() => clearInterval(timer));
const activeProfile = computed(() =>
  data.value?.commands.find(
    (c: any) => c.version === data.value?.lamp.latest?.config_version,
  ),
);
const status: any = {
  pending: "Pendiente",
  sent: "Enviada",
  deferred: "Diferida por la lámpara",
  applied: "Aplicada",
  rejected: "Rechazada",
  superseded: "Sustituida",
};
</script>
<template>
  <section v-if="data">
    <NuxtLink to="/" class="text-link">← Todas las lámparas</NuxtLink>
    <div class="page-heading">
      <div>
        <p class="eyebrow">FICHA DE LUMINARIA</p>
        <h1>{{ data.lamp.name || data.lamp.device_id }}</h1>
        <p class="muted mono">
          {{ data.lamp.device_id }} · {{ data.lamp.gateway_name }}
        </p>
      </div>
      <StatusPills :lamp="data.lamp" />
    </div>
    <div class="stats">
      <div class="stat">
        <span>Voltaje</span
        ><strong
          >{{
            data.lamp.latest
              ? (data.lamp.latest.voltage_mv / 1000).toFixed(3)
              : "—"
          }}
          <small>V</small></strong
        >
      </div>
      <div class="stat">
        <span>Corriente</span
        ><strong
          >{{ data.lamp.latest?.current_ma ?? "—" }} <small>mA</small></strong
        >
      </div>
      <div class="stat">
        <span>Temperatura</span
        ><strong
          >{{
            data.lamp.sensor_valid
              ? (data.lamp.latest.temperature_centi_c / 100).toFixed(1)
              : "—"
          }}
          <small>°C</small></strong
        >
      </div>
      <div class="stat">
        <span>Estimación por tensión</span
        ><strong
          >{{ data.lamp.sensor_valid ? data.lamp.latest.soh_pct : "—" }}
          <small>%</small></strong
        >
      </div>
    </div>
    <p class="notice">
      Última comunicación: {{ dateText(data.lamp.last_seen) }}. El porcentaje no
      mide capacidad ni autonomía.
      {{
        data.lamp.config_capable
          ? ""
          : "Firmware sin soporte de configuración remota; puede usar umbrales fijos de 6 V."
      }}
    </p>
    <p
      v-if="
        !activeProfile ||
        activeProfile.profile_nominal_mv !== data.lamp.nominal_mv
      "
      class="notice"
    >
      El perfil activo no está confirmado para la tensión nominal registrada.
      Conservamos los flags y resultados originales, pero no los presentamos
      como un diagnóstico validado de esta batería.
    </p>
    <div class="tabs chart-ranges" role="group" aria-label="Periodo de los gráficos">
      <button
        v-for="r in ranges"
        :key="r[0]"
        type="button"
        :class="{ selected: range === r[0] }"
        :aria-pressed="range === r[0]"
        @click="range = r[0]"
      >
        {{ r[1] }}
      </button>
    </div>
    <div
      class="grid-2 chart-grid-area"
      :class="{ reloading: series?.range !== range }"
    >
      <LineChart
        v-for="c in [
          { field: 'voltage_mv', label: 'Voltaje de batería', divisor: 1000, unit: 'V' },
          { field: 'current_ma', label: 'Corriente · positivo descarga', divisor: 1, unit: 'mA' },
          { field: 'temperature_centi_c', label: 'Temperatura', divisor: 100, unit: '°C' },
          { field: 'power_mw', label: 'Potencia', divisor: 1000, unit: 'W' },
        ]"
        :key="c.field"
        :series="series"
        :field="c.field"
        :label="c.label"
        :divisor="c.divisor"
        :unit="c.unit"
      />
    </div>
    <div v-if="admin" class="grid-2">
      <form class="panel form-panel" @submit.prevent="save">
        <h2>Identificación y ubicación</h2>
        <label>Nombre<input v-model="edit.name" required /></label
        ><label>Modelo<input v-model="edit.model" /></label
        ><label
          >Tensión nominal de batería (mV)<input
            v-model.number="edit.nominal_mv"
            type="number"
            min="1"
            required /></label
        ><label
          >Nivel<select v-model="edit.location_id">
            <option value="">Sin asignar</option>
            <option
              v-for="l in locations?.filter((x) => x.kind === 'level')"
              :key="l.id"
              :value="l.id"
            >
              {{ l.name }}
            </option>
          </select></label
        ><label
          >Lugar<input
            v-model="edit.place"
            placeholder="Pasillo norte" /></label
        ><button :disabled="busy" class="primary">
          Guardar identificación
        </button>
      </form>
      <form class="panel form-panel" @submit.prevent="configure">
        <h2>Perfil de batería</h2>
        <p>
          Solicitada:
          <strong>{{ data.lamp.desired_version || "Ninguna" }}</strong> ·
          Confirmada:
          <strong>{{ data.lamp.applied_version ?? "Desconocida" }}</strong>
        </p>
        <label
          >Perfil y versión<select v-model="profile" required>
            <option value="">Selecciona un perfil compatible</option>
            <option
              v-for="p in profiles?.filter(
                (p) => p.nominal_mv === data.lamp.nominal_mv,
              )"
              :key="p.version_id"
              :value="p.version_id"
            >
              {{ p.name }} · v{{ p.version }} · {{ p.nominal_mv / 1000 }} V
            </option>
          </select></label
        >
        <p class="muted">
          El cambio se confirma únicamente cuando el nodo informa que lo guardó
          y aplicó. Si está en prueba, debe diferirlo hasta terminar.
        </p>
        <button :disabled="busy || !data.lamp.config_capable" class="primary">
          Solicitar configuración
        </button>
        <p v-if="!data.lamp.config_capable" class="error">
          El firmware no admite configuración remota.
        </p>
      </form>
    </div>
    <p v-if="error" class="error" role="alert">{{ error }}</p>
    <p v-if="message" class="success" role="status">{{ message }}</p>
    <section class="panel">
      <div class="panel-heading"><h2>Configuraciones</h2></div>
      <div class="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Versión</th>
              <th>Perfil</th>
              <th>Estado</th>
              <th>Fecha</th>
              <th>Detalle</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="c in data.commands" :key="c.id">
              <td>{{ c.version }}</td>
              <td>{{ c.profile_name }}</td>
              <td>{{ status[c.status] }}</td>
              <td>{{ dateText(c.applied_at || c.created_at) }}</td>
              <td>
                {{ c.reason || "—" }} <small>{{ c.attempts }} intentos</small>
              </td>
            </tr>
            <tr v-if="!data.commands.length">
              <td colspan="5" class="empty">Sin cambios solicitados</td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
    <section class="panel">
      <div class="panel-heading">
        <h2>Prueba individual</h2>
        <button
          v-if="admin"
          class="primary"
          :disabled="
            busy ||
            !!activeTest ||
            !data.lamp.connected ||
            !data.lamp.gateway_test_capable ||
            data.lamp.testing ||
            !data.lamp.ac ||
            !data.lamp.sensor_valid
          "
          @click="confirmTest = true"
        >
          Ejecutar prueba individual · 30 s
        </button>
      </div>
      <p v-if="!data.lamp.gateway_test_capable" class="notice">
        El C6 necesita firmware con recepción de órdenes MQTT y reloj
        sincronizado para habilitar este botón.
      </p>
      <p v-else-if="!data.lamp.connected" class="notice">
        La lámpara o su gateway están sin comunicación.
      </p>
      <p v-else-if="data.lamp.testing" class="notice">
        La lámpara ya está en prueba.
      </p>
      <p v-else-if="!data.lamp.ac || !data.lamp.sensor_valid" class="notice">
        Se necesita alimentación AC y una lectura válida del sensor para
        solicitar la prueba.
      </p>
      <p class="small muted">
        La prueba solicita al gateway simular la pérdida de AC durante 30
        segundos para evaluar la batería. El resultado debe llegar desde la
        lámpara; un envío por MQTT no confirma una prueba aprobada.
      </p>
      <div
        v-if="confirmTest && admin"
        role="alertdialog"
        aria-label="Confirmar prueba individual"
        class="notice"
      >
        <p>
          ¿Ejecutar una prueba de 30 segundos en «{{
            data.lamp.name || data.lamp.device_id
          }}»?
        </p>
        <div class="actions">
          <button class="primary" :disabled="busy" @click="runTest">
            Confirmar prueba</button
          ><button
            class="secondary"
            :disabled="busy"
            @click="confirmTest = false"
          >
            Cancelar
          </button>
        </div>
      </div>
      <div v-if="data.test_requests?.length" class="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Solicitud</th>
              <th>Estado</th>
              <th>Resultado</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="r in data.test_requests" :key="r.id">
              <td>
                {{ dateText(r.created_at) }}<small>{{ r.id }}</small>
              </td>
              <td>
                {{ testState[r.status] || r.status
                }}<small>{{ r.reason }}</small>
              </td>
              <td>{{ r.result?.result || "—" }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
    <section class="panel">
      <div class="panel-heading">
        <h2>Resultados de pruebas recibidos</h2>
        <NuxtLink :to="'/reportes?lamp=' + data.lamp.id" class="text-link"
          >Exportar →</NuxtLink
        >
      </div>
      <div class="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Fecha</th>
              <th>Resultado original</th>
              <th>Duración</th>
              <th>V mínima</th>
              <th>Configuración</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="t in data.tests" :key="t.id">
              <td>
                {{ dateText(t.measured_at || t.received_at)
                }}<small>{{ t.measured_at ? "Medición" : "Recepción" }}</small>
              </td>
              <td>{{ t.data.result }}</td>
              <td>{{ t.data.duration_s }} s</td>
              <td>{{ t.data.voltage_min_mv / 1000 }} V</td>
              <td>
                {{ t.config_version ?? "Desconocida" }} ·
                {{ t.profile_snapshot?.name || "Perfil desconocido" }}
              </td>
            </tr>
            <tr v-if="!data.tests.length">
              <td colspan="5" class="empty">
                Sin resultados. Esto no equivale a una prueba aprobada.
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
  </section>
  <div v-else class="empty">Lámpara no disponible</div>
</template>
