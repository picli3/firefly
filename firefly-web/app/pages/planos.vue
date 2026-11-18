<script setup lang="ts">
import { mapAppearance } from "#shared/map-state.mjs";
const fetcher = useRequestFetch(),
  { admin, refresh: refreshSession, user } = useSession();
const { data: locations } = await useAsyncData<any[]>("locations", () =>
  fetcher("/api/locations"),
);
const { data: plans, refresh: refreshPlans } = await useAsyncData<any[]>(
  "plans",
  () => fetcher("/api/plans"),
);
const { data: lamps, refresh: refreshLamps } = await useAsyncData<any[]>(
  "map-lamps",
  () => fetcher("/api/lamps"),
);
const { data: gateways, refresh: refreshGateways } = await useAsyncData<any[]>(
  "map-gateways",
  () => fetcher("/api/gateways"),
);
const level = ref(""),
  planId = ref(""),
  plan = ref<any>(null),
  error = ref(""),
  message = ref(""),
  toast = useToast(),
  selected = ref(""),
  selectedGateway = ref(""),
  kind = ref<"lamp" | "gateway">("lamp"),
  placing = ref(false),
  busy = ref(false),
  saving = ref(false),
  live = ref(false),
  lampElapsed = ref(0),
  gatewayElapsed = ref(0),
  file = ref<HTMLInputElement>(),
  svg = ref<SVGSVGElement>(),
  view = ref<[number, number, number, number]>([0, 0, 100, 100]),
  positions = ref<any[]>([]),
  gatewayPositions = ref<any[]>([]);
const levelLamps = computed(() =>
  (lamps.value || []).filter((l) => l.location_id === level.value),
);
const levelGateways = computed(() =>
  (gateways.value || []).filter((g) => g.map_level_id === level.value),
);
const selectedLamp = computed<any>(() =>
  levelLamps.value.find((l) => l.id === selected.value),
);
const gateway = computed<any>(() =>
  gateways.value?.find((g) => g.id === selectedGateway.value),
);
const availablePlans = computed(() =>
  (plans.value || []).filter(
    (p) => p.level_id === level.value && (admin.value || p.active),
  ),
);
const lampMarkers = computed(() =>
  positions.value
    .map((p) => ({
      ...p,
      item: levelLamps.value.find((l) => l.id === p.lamp_id),
    }))
    .filter((p) => p.item && p.x !== null && p.y !== null),
);
const gatewayMarkers = computed(() =>
  gatewayPositions.value
    .map((p) => ({
      ...p,
      item: gateways.value?.find((g) => g.id === p.gateway_id),
    }))
    .filter((p) => p.item && p.x !== null && p.y !== null),
);
const listedGateways = computed(() =>
  admin.value ? gateways.value || [] : levelGateways.value,
);
const lampLook = (l: any) => mapAppearance(l, "lamp", lampElapsed.value);
const gatewayLook = (g: any) =>
  mapAppearance(g, "gateway", gatewayElapsed.value);
function syncPositions() {
  positions.value = levelLamps.value.map((l) => ({
    lamp_id: l.id,
    x: l.x,
    y: l.y,
  }));
  gatewayPositions.value = levelGateways.value.map((g) => ({
    gateway_id: g.id,
    x: g.x,
    y: g.y,
  }));
}
watch(level, () => {
  planId.value =
    availablePlans.value.find((p) => p.active)?.id ||
    availablePlans.value[0]?.id ||
    "";
  selected.value = "";
  selectedGateway.value = "";
  placing.value = false;
  plan.value = null;
});
let loadVersion = 0;
watch(planId, async () => {
  const version = ++loadVersion;
  plan.value = null;
  if (!planId.value) return;
  try {
    const next = await $fetch("/api/plans/" + planId.value);
    if (version !== loadVersion) return;
    plan.value = next;
    resetView();
    syncPositions();
    error.value = "";
  } catch (e) {
    if (version === loadVersion) error.value = errorText(e);
  }
});
watch([lamps, gateways], () => {
  if (plan.value?.active && !placing.value && !saving.value) syncPositions();
});
watch([kind, selected, selectedGateway], () => {
  placing.value = false;
});
function resetView() {
  if (plan.value) {
    const [x, y, w, h] = plan.value.geometry.bounds;
    view.value = [x - w * 0.05, -y - h - h * 0.05, w * 1.1, h * 1.1];
  }
}
function screenPoint(e: MouseEvent | WheelEvent) {
  const point = svg.value!.createSVGPoint();
  point.x = e.clientX;
  point.y = e.clientY;
  return point.matrixTransform(svg.value!.getScreenCTM()!.inverse());
}
function wheel(e: WheelEvent) {
  const p = screenPoint(e),
    factor = e.deltaY > 0 ? 1.12 : 0.89;
  const [x, y, w, h] = view.value;
  if (w * factor < 0.00001 || w * factor > 1e12) return;
  view.value = [
    p.x - (p.x - x) * factor,
    p.y - (p.y - y) * factor,
    w * factor,
    h * factor,
  ];
}
let drag: any = null,
  moved = false;
function down(e: PointerEvent) {
  if (e.button !== 0) return;
  drag = { x: e.clientX, y: e.clientY, view: [...view.value] };
  moved = false;
  svg.value?.setPointerCapture(e.pointerId);
}
function move(e: PointerEvent) {
  if (!drag || placing.value) return;
  const matrix = svg.value!.getScreenCTM()!,
    dx = e.clientX - drag.x,
    dy = e.clientY - drag.y;
  if (Math.abs(dx) + Math.abs(dy) > 3) moved = true;
  view.value = [
    drag.view[0] - dx / matrix.a,
    drag.view[1] - dy / matrix.d,
    drag.view[2],
    drag.view[3],
  ];
}
async function click(e: MouseEvent) {
  if (moved || !placing.value || !admin.value || saving.value) return;
  const p = screenPoint(e),
    isGateway = kind.value === "gateway",
    item = isGateway ? gateway.value : selectedLamp.value;
  if (!item) return;
  const targetKind = kind.value;
  error.value = "";
  saving.value = true;
  try {
    if (plan.value.active) {
      if (isGateway) {
        await $fetch("/api/gateways/" + item.id + "/placement", {
          method: "PATCH",
          body: { level_id: level.value, x: p.x, y: -p.y },
        });
        await refreshGateways();
      } else {
        await $fetch("/api/lamps/" + item.id, {
          method: "PATCH",
          body: {
            name: item.name || item.device_id,
            model: item.model,
            location_id: item.location_id,
            place: item.place,
            x: p.x,
            y: -p.y,
          },
        });
        await refreshLamps();
      }
      placing.value = false;
      syncPositions();
      message.value =
        (isGateway ? "Gateway" : "Lámpara") + ": posición guardada";
    } else {
      const list =
          targetKind === "gateway" ? gatewayPositions.value : positions.value,
        key = isGateway ? "gateway_id" : "lamp_id";
      let pos = list.find((pos) => pos[key] === item.id);
      if (!pos) {
        pos = { [key]: item.id, x: null, y: null };
        list.push(pos);
      }
      pos.x = p.x;
      pos.y = -p.y;
      placing.value = false;
      message.value = "Posición en borrador. Se guardará al activar el plano.";
    }
  } catch (e) {
    error.value = errorText(e);
  } finally {
    saving.value = false;
  }
}
function clearPosition() {
  const list =
      kind.value === "gateway" ? gatewayPositions.value : positions.value,
    key = kind.value === "gateway" ? "gateway_id" : "lamp_id",
    id = kind.value === "gateway" ? selectedGateway.value : selected.value;
  const p = list.find((p) => p[key] === id);
  if (p) {
    p.x = null;
    p.y = null;
  }
}
function selectLamp(id: string) {
  kind.value = "lamp";
  selected.value = id;
  placing.value = false;
}
function selectGateway(id: string) {
  kind.value = "gateway";
  selectedGateway.value = id;
  placing.value = false;
}
async function upload() {
  if (!file.value?.files?.[0]) return;
  busy.value = true;
  error.value = "";
  try {
    const body = new FormData();
    body.append("file", file.value.files[0]);
    body.append("level_id", level.value);
    const result: any = await $fetch("/api/plans", { method: "POST", body });
    await refreshPlans();
    planId.value = result.id;
    message.value =
      "Borrador cargado. Revisa posiciones de lámparas y gateways antes de activarlo.";
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
async function activate() {
  busy.value = true;
  error.value = "";
  try {
    await $fetch("/api/plans/" + planId.value + "/activate", {
      method: "POST",
      body: {
        positions: positions.value,
        gateway_positions: gatewayPositions.value,
      },
    });
    await Promise.all([refreshPlans(), refreshLamps(), refreshGateways()]);
    plan.value.active = true;
    syncPositions();
    message.value =
      "Plano activado y posiciones de lámparas y gateways confirmadas";
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
let events: EventSource | undefined, timer: ReturnType<typeof setInterval>;
onMounted(() => {
  const visiblePlans = (plans.value || []).filter(
    (p) =>
      (admin.value || p.active) &&
      locations.value?.some((l) => l.kind === "level" && l.id === p.level_id),
  );
  const firstPlan = visiblePlans.find((p) => p.active) || visiblePlans[0];
  if (firstPlan) level.value = firstPlan.level_id;
  timer = setInterval(() => {
    lampElapsed.value++;
    gatewayElapsed.value++;
  }, 1000);
  events = new EventSource("/api/events");
  events.addEventListener("snapshot", (e) => {
    lamps.value = JSON.parse((e as MessageEvent).data);
    lampElapsed.value = 0;
    live.value = true;
  });
  events.addEventListener("gateways", (e) => {
    gateways.value = JSON.parse((e as MessageEvent).data);
    gatewayElapsed.value = 0;
  });
  events.onerror = async () => {
    live.value = false;
    await refreshSession();
    if (!user.value) {
      events?.close();
      await navigateTo("/login");
    }
  };
});
onBeforeUnmount(() => {
  events?.close();
  clearInterval(timer);
});
// Colores de marcador (shared/map-state) traducidos a tokens: siguen el modo claro/oscuro.
const markerFill = (c: string) =>
  ({
    "#16785c": "var(--ff-chart-line)",
    "#316cad": "var(--ff-map-gateway)",
    "#85928c": "var(--ff-map-inactive)",
  })[c] || c;
// Los mensajes de éxito se muestran como notificación.
watch(message, (m) => {
  if (m) toast.add({ title: m, color: "success", icon: "i-lucide-circle-check" });
});
</script>
<template>
  <section>
    <div class="page-heading">
      <div>
        <p class="eyebrow">DISTRIBUCIÓN DE DISPOSITIVOS</p>
        <h1>Planos y ubicaciones</h1>
        <p class="muted">
          Localiza lámparas y gateways y consulta su comunicación.
        </p>
      </div>
      <UBadge
        :color="live ? 'success' : 'neutral'"
        variant="subtle"
        :icon="live ? 'i-lucide-radio' : 'i-lucide-wifi-off'"
        :label="live ? 'Actualización en vivo' : 'Actualización interrumpida'"
      />
    </div>
    <div class="filter-bar">
      <label
        >Nivel<FfSelect
          v-model="level"
          empty-label="Selecciona un nivel"
          :items="
            (locations || [])
              .filter((l) => l.kind === 'level')
              .map((l) => ({
                value: l.id,
                label: `${locations?.find((b) => b.id === l.parent_id)?.name || ''} / ${l.name}`,
              }))
          "
        /></label
      ><label v-if="level"
        >Plano<FfSelect
          v-model="planId"
          empty-label="Sin plano"
          :items="
            availablePlans.map((p: any) => ({
              value: p.id,
              label: `${p.filename} · ${p.active ? 'Activo' : 'Borrador'}`,
            }))
          "
        /></label
      >
    </div>
    <form
      v-if="admin && level"
      class="panel upload-bar"
      @submit.prevent="upload"
    >
      <label
        >Subir plano DXF 2D · hasta 5 MiB<input
          ref="file"
          type="file"
          accept=".dxf"
          required /></label
      ><UButton
        type="submit"
        color="neutral"
        variant="outline"
        icon="i-lucide-upload"
        label="Cargar como borrador"
        :loading="busy"
      />
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
    <div v-if="plan" class="map-layout">
      <section class="panel map-panel">
        <div class="panel-heading">
          <h2>{{ plan.filename }}</h2>
          <div class="actions">
            <UBadge
              :color="plan.active ? 'success' : 'warning'"
              variant="subtle"
              :icon="plan.active ? 'i-lucide-circle-check' : 'i-lucide-pencil'"
              :label="plan.active ? 'Plano activo' : 'Borrador: revisar posiciones'"
            />
            <UButton
              color="neutral"
              variant="outline"
              size="sm"
              icon="i-lucide-scan"
              label="Ajustar vista"
              @click="resetView"
            />
            <UButton
              :href="'/api/plans/' + planId + '/download'"
              external
              color="neutral"
              variant="ghost"
              size="sm"
              icon="i-lucide-download"
              label="DXF"
            />
          </div>
        </div>
        <div class="map-legend" aria-label="Leyenda del plano">
          <span
            ><i class="legend-circle" style="background: var(--ff-chart-line)" />Lámpara
            activa</span
          ><span
            ><i class="legend-circle" style="background: var(--ff-map-inactive)" />Lámpara
            inactiva</span
          ><span
            ><i class="legend-square" style="background: var(--ff-map-gateway)" />Gateway
            conectado</span
          ><span
            ><i class="legend-square" style="background: var(--ff-map-inactive)" />Gateway sin
            conexión</span
          >
        </div>
        <p class="map-reception-note">
          Activa significa telemetría reciente, no luz encendida. Sin
          actualización durante tres intervalos esperados pasa a inactiva; si el
          gateway pierde conexión, no se puede confirmar la actividad de sus
          lámparas.
        </p>
        <UAlert
          v-for="warning in plan.warnings"
          :key="warning"
          class="panel-alert"
          color="warning"
          variant="subtle"
          icon="i-lucide-triangle-alert"
          :description="warning"
        />
        <div class="map-canvas" :class="{ placing }">
          <svg
            ref="svg"
            :viewBox="view.join(' ')"
            preserveAspectRatio="xMidYMid meet"
            aria-label="Plano del nivel con lámparas y gateways"
            @wheel.prevent="wheel"
            @pointerdown="down"
            @pointermove="move"
            @pointerup="drag = null"
            @pointercancel="drag = null"
            @click="click"
          >
            <g
              transform="scale(1,-1)"
              stroke="var(--ff-text-faint)"
              fill="none"
              :stroke-width="view[2] / 1100"
            >
              <template v-for="(s, i) in plan.geometry.shapes" :key="i">
                <circle
                  v-if="s.type === 'circle'"
                  :cx="s.center[0]"
                  :cy="s.center[1]"
                  :r="s.radius"
                />
                <polyline
                  v-else
                  :points="
                    [...s.points, ...(s.closed ? [s.points[0]] : [])]
                      .map((p) => p.join(','))
                      .join(' ')
                  "
                />
              </template>
            </g>
            <g
              v-for="p in lampMarkers"
              :key="'lamp-' + p.lamp_id"
              class="map-marker"
              role="button"
              tabindex="0"
              :aria-label="
                (p.item.name || p.item.device_id) +
                ' · ' +
                lampLook(p.item).label
              "
              :data-device-id="p.lamp_id"
              data-device-kind="lamp"
              :data-connection="
                lampLook(p.item).connected ? 'active' : 'inactive'
              "
              @pointerdown.stop
              @click.stop="selectLamp(p.lamp_id)"
              @keydown.enter.prevent="selectLamp(p.lamp_id)"
              @keydown.space.prevent="selectLamp(p.lamp_id)"
            >
              <title>
                {{ p.item.name || p.item.device_id }} ·
                {{ lampLook(p.item).label }} · {{ p.item.gateway_name }}
              </title>
              <circle
                :cx="p.x"
                :cy="-p.y"
                :r="view[2] / 90"
                :fill="markerFill(lampLook(p.item).color)"
                stroke="var(--ff-surface)"
                :stroke-width="view[2] / 450"
              />
              <circle
                v-if="p.item.alert || p.item.critical"
                :cx="p.x + view[2] / 100"
                :cy="-p.y - view[2] / 100"
                :r="view[2] / 170"
                fill="var(--ff-map-warning)"
                stroke="var(--ff-surface)"
                :stroke-width="view[2] / 600"
              >
                <title>Última alerta recibida</title>
              </circle>
              <text
                :x="p.x + view[2] / 60"
                :y="-p.y + view[3] / 150"
                :font-size="view[2] / 65"
                fill="var(--ff-text)"
              >
                {{ p.item.name || "Lámpara" }}
              </text>
            </g>
            <g
              v-for="p in gatewayMarkers"
              :key="'gateway-' + p.gateway_id"
              class="map-marker"
              role="button"
              tabindex="0"
              :aria-label="
                'Gateway ' + p.item.name + ' · ' + gatewayLook(p.item).label
              "
              :data-device-id="p.gateway_id"
              data-device-kind="gateway"
              :data-connection="
                gatewayLook(p.item).connected ? 'active' : 'inactive'
              "
              @pointerdown.stop
              @click.stop="selectGateway(p.gateway_id)"
              @keydown.enter.prevent="selectGateway(p.gateway_id)"
              @keydown.space.prevent="selectGateway(p.gateway_id)"
            >
              <title>
                Gateway {{ p.item.name }} · {{ gatewayLook(p.item).label }}
              </title>
              <rect
                :x="p.x - view[2] / 75"
                :y="-p.y - view[2] / 75"
                :width="view[2] / 37.5"
                :height="view[2] / 37.5"
                :rx="view[2] / 300"
                :fill="markerFill(gatewayLook(p.item).color)"
                stroke="var(--ff-surface)"
                :stroke-width="view[2] / 450"
              />
              <text
                :x="p.x"
                :y="-p.y + view[2] / 180"
                text-anchor="middle"
                :font-size="view[2] / 60"
                fill="white"
                font-weight="bold"
              >
                G
              </text>
              <text
                :x="p.x + view[2] / 50"
                :y="-p.y + view[3] / 150"
                :font-size="view[2] / 65"
                fill="var(--ff-text)"
              >
                Gateway · {{ p.item.name }}
              </text>
            </g></svg
          ><span class="map-help">{{
            placing
              ? kind === "gateway"
                ? "Haz clic para ubicar el gateway"
                : "Haz clic para ubicar la lámpara"
              : "Arrastra para mover · rueda para ampliar"
          }}</span>
        </div>
      </section>
      <aside class="panel form-panel">
        <h2>Dispositivos del plano</h2>
        <label
          >Tipo de dispositivo<FfSelect
            v-model="kind"
            :items="[
              { value: 'lamp', label: 'Lámparas' },
              { value: 'gateway', label: 'Gateways' },
            ]"
          /></label
        >
        <template v-if="kind === 'lamp'"
          ><label
            >Lámpara<FfSelect
              v-model="selected"
              empty-label="Selecciona una lámpara"
              :items="
                levelLamps.map((l: any) => ({
                  value: l.id,
                  label: `${l.name || l.device_id} · ${lampLook(l).label}`,
                }))
              "
            /></label
          ><template v-if="selectedLamp"
            ><UBadge
              class="self-start"
              :color="lampLook(selectedLamp).connected ? 'success' : 'neutral'"
              variant="subtle"
              :label="lampLook(selectedLamp).label"
            />
            <p class="small muted">
              Última telemetría: {{ dateText(selectedLamp.last_seen) }}.
              Gateway: {{ selectedLamp.gateway_name }}.
            </p>
            <StatusPills :lamp="selectedLamp" /><UButton
              :to="'/lamparas/' + selected"
              class="self-start"
              variant="ghost"
              size="sm"
              trailing-icon="i-lucide-arrow-right"
              label="Abrir ficha"
            /><UButton
              v-if="admin"
              :color="placing ? 'neutral' : 'primary'"
              :variant="placing ? 'outline' : 'soft'"
              :icon="placing ? 'i-lucide-x' : 'i-lucide-locate-fixed'"
              :label="placing ? 'Cancelar colocación' : 'Colocar o mover lámpara'"
              :disabled="saving"
              block
              @click="placing = !placing"
            /></template
          >
          <p class="small muted">
            {{ positions.filter((p) => p.x === null).length }} lámparas sin
            posición. Asigna el nivel desde la ficha de cada lámpara.
          </p></template
        >
        <template v-else
          ><label
            >Gateway<FfSelect
              v-model="selectedGateway"
              empty-label="Selecciona un gateway"
              :items="
                listedGateways.map((g: any) => ({
                  value: g.id,
                  label: `${g.name} · ${gatewayLook(g).label}`,
                }))
              "
            /></label
          ><template v-if="gateway"
            ><UBadge
              class="self-start"
              :color="gatewayLook(gateway).connected ? 'info' : 'neutral'"
              variant="subtle"
              :label="gatewayLook(gateway).label"
            />
            <p class="small muted mono">{{ gateway.id }}</p>
            <p class="small muted">
              Última comunicación: {{ dateText(gateway.last_seen) }}.
            </p>
            <UAlert
              v-if="gateway.map_level_id && gateway.map_level_id !== level"
              color="info"
              variant="subtle"
              icon="i-lucide-info"
              :description="`Ubicado en ${locations?.find((l) => l.id === gateway.map_level_id)?.name || 'otro nivel'}. Al colocarlo aquí se trasladará al nivel seleccionado.`"
            />
            <UAlert
              v-else-if="!gateway.map_level_id"
              color="neutral"
              variant="subtle"
              icon="i-lucide-map-pin-off"
              description="Este gateway aún no tiene nivel asignado en el plano."
            />
            <UButton
              v-if="admin"
              :color="placing ? 'neutral' : 'primary'"
              :variant="placing ? 'outline' : 'soft'"
              :icon="placing ? 'i-lucide-x' : 'i-lucide-locate-fixed'"
              :label="placing ? 'Cancelar colocación' : 'Colocar o mover gateway en este nivel'"
              :disabled="saving"
              block
              @click="placing = !placing"
            /></template
          >
          <p class="small muted">
            {{ levelGateways.length }} gateways asignados a este nivel;
            {{ gatewayPositions.filter((p) => p.x === null).length }} sin
            posición.
          </p>
          <p class="small muted">
            Un gateway puede atender lámparas de otros niveles. Su marcador
            indica dónde está instalado.
          </p></template
        >
        <button
          v-if="
            admin &&
            !plan.active &&
            ((kind === 'lamp' && selectedLamp) ||
              (kind === 'gateway' && gateway))
          "
          class="ghost-danger"
          @click="clearPosition"
        >
          Dejar sin posición en el borrador
        </button>
        <template v-if="admin && !plan.active"
          ><UAlert
            color="warning"
            variant="subtle"
            icon="i-lucide-triangle-alert"
            description="Revisa las posiciones de lámparas y gateways. Las no ubicadas quedarán sin posición al activar."
          />
          <UButton
            icon="i-lucide-circle-check"
            label="Confirmar posiciones y activar"
            :loading="busy || saving"
            block
            @click="activate"
          /></template
        >
      </aside>
    </div>
    <div v-else class="panel empty">
      <UIcon name="i-lucide-map" class="empty-icon" />
      <strong>{{
        level
          ? "Este nivel aún no tiene un plano disponible"
          : "Selecciona un nivel"
      }}</strong>
      <p>
        El administrador puede cargar un DXF para ubicar las lámparas y los
        gateways.
      </p>
    </div>
  </section>
</template>
<style scoped>
.map-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  padding: 14px 20px 6px;
  font-size: 12px;
  color: var(--ff-text-muted);
}
.map-legend span {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.legend-circle,
.legend-square {
  display: inline-block;
  width: 11px;
  height: 11px;
  flex: none;
}
.legend-circle {
  border-radius: 50%;
}
.legend-square {
  border-radius: 2px;
}
.map-reception-note {
  font-size: 12px;
  color: var(--ff-text-muted);
  line-height: 1.6;
  padding: 0 20px;
  margin: 8px 0 14px;
}
.map-marker:focus-visible {
  outline: 2px solid var(--ff-sidebar);
  outline-offset: 4px;
}
</style>
