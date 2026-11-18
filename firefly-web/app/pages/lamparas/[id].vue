<script setup lang="ts">
const route = useRoute(),
  router = useRouter(),
  toast = useToast(),
  fetcher = useRequestFetch(),
  { admin } = useSession();
// Pestañas: la elegida queda en la URL (?tab=) para poder compartir el enlace.
const tabs = [
  { label: "Resumen", value: "resumen", icon: "i-lucide-activity" },
  { label: "Pruebas", value: "pruebas", icon: "i-lucide-flask-conical" },
  { label: "Configuración", value: "configuracion", icon: "i-lucide-sliders-horizontal" },
];
const tab = computed({
  get: () =>
    tabs.some((t) => t.value === route.query.tab)
      ? String(route.query.tab)
      : "resumen",
  set: (v: string | number) =>
    router.replace({ query: { ...route.query, tab: String(v) } }),
});
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
  busy = ref(false);
function ok(title: string, description?: string) {
  toast.add({ title, description, color: "success", icon: "i-lucide-circle-check" });
}
function fail(e: unknown) {
  toast.add({
    title: "No se pudo completar",
    description: errorText(e),
    color: "error",
    icon: "i-lucide-circle-x",
  });
}
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
    ok("Lámpara actualizada");
  } catch (e) {
    fail(e);
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
  testRequestId.value ||= crypto.randomUUID();
  try {
    await $fetch("/api/lamps/" + route.params.id + "/test", {
      method: "POST",
      body: { request_id: testRequestId.value },
    });
    testRequestId.value = null;
    confirmTest.value = false;
    await refresh();
    ok(
      "Prueba individual solicitada",
      "El estado y el resultado aparecen en la tabla de solicitudes.",
    );
  } catch (e) {
    fail(e);
  } finally {
    busy.value = false;
  }
}
async function configure() {
  busy.value = true;
  try {
    await $fetch("/api/lamps/" + route.params.id + "/configure", {
      method: "POST",
      body: { profile_version_id: profile.value },
    });
    profileTouched.value = false;
    await refresh();
    ok(
      "Configuración solicitada",
      "Se mostrará como actual cuando la lámpara confirme que la aplicó.",
    );
  } catch (e) {
    fail(e);
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
// Profile the node has confirmed: what its telemetry reports, else the last ACK.
const currentCommand = computed(
  () =>
    activeProfile.value ||
    data.value?.commands.find(
      (c: any) =>
        c.version === data.value?.lamp.applied_version && c.status === "applied",
    ),
);
const desiredCommand = computed(() =>
  data.value?.commands.find(
    (c: any) => c.version === data.value?.lamp.desired_version,
  ),
);
const pendingCommand = computed(() =>
  desiredCommand.value &&
  desiredCommand.value.version !== currentCommand.value?.version &&
  ["pending", "sent", "deferred"].includes(desiredCommand.value.status)
    ? desiredCommand.value
    : null,
);
// Preselect the profile in effect (or the one on its way) until the user picks another.
const profileTouched = ref(false);
watch(
  () => (pendingCommand.value || currentCommand.value)?.profile_version_id,
  (id) => {
    if (id && !profileTouched.value) profile.value = id;
  },
  { immediate: true },
);
const profileUnchanged = computed(
  () =>
    !!profile.value &&
    profile.value ===
      (pendingCommand.value || currentCommand.value)?.profile_version_id,
);
const status: any = {
  pending: "Pendiente",
  sent: "Enviada",
  deferred: "Diferida por la lámpara",
  applied: "Aplicada",
  rejected: "Rechazada",
  superseded: "Sustituida",
};
type BadgeColor = "success" | "info" | "warning" | "error" | "neutral";
const statusColor: Record<string, BadgeColor> = {
  pending: "neutral",
  sent: "info",
  deferred: "warning",
  applied: "success",
  rejected: "error",
  superseded: "neutral",
};
const testColor: Record<string, BadgeColor> = {
  pending: "info",
  sent: "info",
  accepted: "info",
  completed: "success",
  rejected: "error",
  unknown: "warning",
  expired: "neutral",
  cancelled: "neutral",
};
const testBlocked = computed(() => {
  const l = data.value?.lamp;
  if (!l) return "";
  if (!l.gateway_test_capable)
    return "El C6 necesita firmware con recepción de órdenes MQTT y reloj sincronizado para habilitar la prueba.";
  if (!l.connected) return "La lámpara o su gateway están sin comunicación.";
  if (l.testing) return "La lámpara ya está en prueba.";
  if (!l.ac || !l.sensor_valid)
    return "Se necesita alimentación AC y una lectura válida del sensor para solicitar la prueba.";
  return "";
});
const profileUnconfirmed = computed(
  () =>
    !activeProfile.value ||
    activeProfile.value.profile_nominal_mv !== data.value?.lamp.nominal_mv,
);
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
      <div class="lamp-heading-side">
        <StatusPills :lamp="data.lamp" />
        <span class="muted small"
          >Última comunicación: {{ dateText(data.lamp.last_seen) }}</span
        >
      </div>
    </div>

    <UTabs
      v-model="tab"
      :items="tabs"
      :content="false"
      variant="link"
      color="primary"
      class="lamp-tabs"
    />

    <!-- RESUMEN -->
    <template v-if="tab === 'resumen'">
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
      <div class="lamp-alerts">
        <UAlert
          v-if="profileUnconfirmed"
          color="warning"
          variant="subtle"
          icon="i-lucide-triangle-alert"
          title="Perfil no confirmado para esta batería"
          description="El perfil activo no está confirmado para la tensión nominal registrada. Se conservan los flags y resultados originales, pero no se presentan como un diagnóstico validado."
        />
        <UAlert
          v-if="!data.lamp.config_capable"
          color="neutral"
          variant="subtle"
          icon="i-lucide-cpu"
          title="Firmware sin configuración remota"
          description="Puede estar usando los umbrales fijos de 6 V del firmware."
        />
        <UAlert
          color="info"
          variant="subtle"
          icon="i-lucide-info"
          description="El porcentaje es una estimación por tensión: no mide capacidad ni autonomía."
        />
      </div>
      <div class="chart-toolbar">
        <UFieldGroup aria-label="Periodo de los gráficos">
          <UButton
            v-for="r in ranges"
            :key="r[0]"
            :label="r[1]"
            size="sm"
            :color="range === r[0] ? 'primary' : 'neutral'"
            :variant="range === r[0] ? 'solid' : 'outline'"
            :aria-pressed="range === r[0]"
            @click="range = r[0]"
          />
        </UFieldGroup>
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
    </template>

    <!-- PRUEBAS -->
    <template v-else-if="tab === 'pruebas'">
      <section class="panel">
        <div class="panel-heading">
          <div>
            <h2>Prueba individual</h2>
            <p class="small muted panel-sub">
              Simula la pérdida de AC durante 30 s para evaluar la batería. Solo
              el resultado enviado por la lámpara confirma la prueba.
            </p>
          </div>
          <UButton
            v-if="admin"
            icon="i-lucide-play"
            label="Ejecutar prueba · 30 s"
            :loading="busy"
            :disabled="busy || !!activeTest || !!testBlocked"
            @click="confirmTest = true"
          />
        </div>
        <UAlert
          v-if="testBlocked"
          class="panel-alert"
          color="neutral"
          variant="subtle"
          icon="i-lucide-circle-pause"
          :description="testBlocked"
        />
        <UAlert
          v-else-if="activeTest"
          class="panel-alert"
          color="info"
          variant="subtle"
          icon="i-lucide-loader-circle"
          :description="'Prueba en curso: ' + (testState[activeTest.status] || activeTest.status)"
        />
        <div class="table-scroll">
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
                  {{ dateText(r.created_at) }}<small class="mono">{{ r.id }}</small>
                </td>
                <td>
                  <UBadge
                    :color="testColor[r.status] || 'neutral'"
                    variant="subtle"
                    :label="testState[r.status] || r.status"
                  /><small>{{ r.reason }}</small>
                </td>
                <td>
                  <UBadge
                    v-if="r.result?.result"
                    :color="r.result.result === 'PASS' ? 'success' : 'error'"
                    variant="subtle"
                    :label="r.result.result"
                  />
                  <span v-else class="muted">—</span>
                </td>
              </tr>
              <tr v-if="!data.test_requests?.length">
                <td colspan="3" class="empty">Sin solicitudes de prueba</td>
              </tr>
            </tbody>
          </table>
        </div>
      </section>
      <section class="panel">
        <div class="panel-heading">
          <h2>Resultados de pruebas recibidos</h2>
          <UButton
            :to="'/reportes?lamp=' + data.lamp.id"
            variant="ghost"
            size="sm"
            trailing-icon="i-lucide-arrow-right"
            label="Exportar"
          />
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
                <td>
                  <UBadge
                    :color="t.data.result === 'PASS' ? 'success' : 'error'"
                    variant="subtle"
                    :label="t.data.result"
                  />
                </td>
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
    </template>

    <!-- CONFIGURACIÓN -->
    <template v-else>
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
          >
          <UButton
            type="submit"
            icon="i-lucide-save"
            label="Guardar identificación"
            :loading="busy"
            class="self-start"
          />
        </form>
        <form class="panel form-panel" @submit.prevent="configure">
          <h2>Perfil de batería</h2>
          <div class="profile-current">
            <span class="muted">Perfil actual en la lámpara</span>
            <strong v-if="currentCommand"
              >{{ currentCommand.profile_name }} · v{{
                currentCommand.profile_version
              }}
              · {{ currentCommand.profile_nominal_mv / 1000 }} V</strong
            >
            <strong v-else>Sin perfil confirmado</strong>
            <small v-if="currentCommand" class="muted"
              >Configuración n.º {{ currentCommand.version }} · confirmada
              {{ dateText(currentCommand.applied_at || currentCommand.created_at) }}</small
            >
            <small v-else class="muted">{{
              data.lamp.config_capable
                ? "La lámpara aún no ha confirmado ninguna configuración."
                : "Usa los umbrales fijos de su firmware."
            }}</small>
          </div>
          <UAlert
            v-if="pendingCommand"
            color="info"
            variant="subtle"
            icon="i-lucide-clock"
            :title="`Cambio pendiente: ${pendingCommand.profile_name} · v${pendingCommand.profile_version}`"
            :description="`${status[pendingCommand.status]}. Se mostrará como actual cuando la lámpara confirme que lo aplicó.`"
          />
          <label
            >Perfil y versión<select
              v-model="profile"
              required
              @change="profileTouched = true"
            >
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
          <p class="muted small">
            El cambio se confirma únicamente cuando el nodo informa que lo guardó
            y aplicó. Si está en prueba, lo difiere hasta terminar.
          </p>
          <UAlert
            v-if="!data.lamp.config_capable"
            color="error"
            variant="subtle"
            icon="i-lucide-circle-x"
            description="El firmware no admite configuración remota."
          />
          <UButton
            type="submit"
            icon="i-lucide-send"
            :loading="busy"
            :disabled="busy || !data.lamp.config_capable || profileUnchanged"
            class="self-start"
            :label="
              profileUnchanged
                ? pendingCommand
                  ? 'Cambio ya solicitado'
                  : 'Perfil ya aplicado'
                : 'Solicitar configuración'
            "
          />
        </form>
      </div>
      <section class="panel">
        <div class="panel-heading"><h2>Historial de configuraciones</h2></div>
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
                <td>
                  <UBadge
                    :color="statusColor[c.status] || 'neutral'"
                    variant="subtle"
                    :label="status[c.status] || c.status"
                  />
                </td>
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
    </template>

    <UModal
      v-model:open="confirmTest"
      title="Confirmar prueba individual"
      :description="`Se simulará la pérdida de AC durante 30 segundos en «${data.lamp.name || data.lamp.device_id}».`"
    >
      <template #footer>
        <div class="modal-actions">
          <UButton
            color="neutral"
            variant="outline"
            label="Cancelar"
            :disabled="busy"
            @click="confirmTest = false"
          />
          <UButton
            icon="i-lucide-play"
            label="Confirmar prueba"
            :loading="busy"
            @click="runTest"
          />
        </div>
      </template>
    </UModal>
  </section>
  <div v-else class="empty">Lámpara no disponible</div>
</template>
