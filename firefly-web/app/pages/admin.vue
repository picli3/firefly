<script setup lang="ts">
const fetcher = useRequestFetch(),
  error = ref(""),
  message = ref(""),
  busy = ref(false),
  tab = ref("locations"),
  editing = ref<any>(null);
const provisioning = ref<any>(null),
  provisioningHeading = ref("");
const pendingTrash = ref<any>(null);
const { data, refresh } = await useAsyncData<any>("admin", async () => {
  const [users, locations, gateways, profiles, lamps, audits, trash] =
    await Promise.all(
      [
        "/api/users",
        "/api/locations",
        "/api/gateways",
        "/api/profiles",
        "/api/lamps",
        "/api/audits",
        "/api/trash",
      ].map((p) => fetcher(p)),
    );
  return { users, locations, gateways, profiles, lamps, audits, trash };
});
const form = reactive<any>({}),
  tabs: [string, string][] = [
    ["locations", "Ubicaciones"],
    ["gateways", "Gateways"],
    ["profiles", "Perfiles de batería"],
    ["users", "Usuarios"],
    ["lamps", "Lámparas"],
    ["audits", "Auditoría"],
    ["trash", "Papelera"],
  ];
const toast = useToast();
const tabIcons: Record<string, string> = {
  locations: "i-lucide-map-pin",
  gateways: "i-lucide-router",
  profiles: "i-lucide-battery-medium",
  users: "i-lucide-users",
  lamps: "i-lucide-lamp",
  audits: "i-lucide-history",
  trash: "i-lucide-trash-2",
};
const tabItems = tabs.map(([value, label]) => ({ value, label, icon: tabIcons[value] }));
// Los mensajes de éxito se muestran como notificación; los errores, además, en línea.
watch(message, (m) => {
  if (m) toast.add({ title: m, color: "success", icon: "i-lucide-circle-check" });
});
const trashOpen = computed({
  get: () => !!pendingTrash.value,
  set: (v) => {
    if (!v) pendingTrash.value = null;
  },
});
function reset(row: any = null) {
  editing.value = row;
  Object.keys(form).forEach((k) => delete form[k]);
  Object.assign(
    form,
    tab.value === "locations"
      ? { name: "", kind: "site", parent_id: "" }
      : tab.value === "gateways"
        ? {
            id: "",
            name: "",
            location_id: "",
            password: "",
            active: true,
            report_interval_s: 60,
          }
        : tab.value === "users"
          ? { name: "", email: "", password: "", role: "monitor", active: true }
          : tab.value === "lamps"
            ? { name: "", device_id: "", gateway_id: "" }
            : {
                name: "",
                chemistry: "",
                nominal_mv: 6000,
                thresholds: {
                  critical_mv: 5250,
                  alert_mv: 5500,
                  attention_mv: 5850,
                  test_min_mv: 5250,
                  full_mv: 6300,
                  ac_empty_mv: 5500,
                  dc_empty_mv: 5250,
                },
              },
  );
  if (row) Object.assign(form, JSON.parse(JSON.stringify(row)));
  error.value = "";
  message.value = "";
}
watch(
  tab,
  () => {
    pendingTrash.value = null;
    reset();
  },
  { immediate: true },
);
const thresholdNames: any = {
  critical_mv: "Voltaje crítico",
  alert_mv: "Alerta",
  attention_mv: "Atención",
  test_min_mv: "Mínimo de prueba",
  full_mv: "Referencia de lleno",
  ac_empty_mv: "Referencia vacía con AC",
  dc_empty_mv: "Referencia vacía en batería",
};
async function submit() {
  error.value = "";
  message.value = "";
  busy.value = true;
  try {
    let body: any,
      url = "/api/" + tab.value,
      method: any = editing.value ? "PATCH" : "POST";
    if (tab.value === "locations")
      body = {
        name: form.name,
        kind: form.kind,
        parent_id: form.parent_id || null,
      };
    else if (tab.value === "gateways")
      body = editing.value
        ? {
            name: form.name,
            location_id: form.location_id || null,
            active: form.active,
            report_interval_s: Number(form.report_interval_s),
            ...(form.password ? { password: form.password } : {}),
          }
        : {
            id: form.id,
            name: form.name,
            location_id: form.location_id || null,
            ...(form.password ? { password: form.password } : {}),
          };
    else if (tab.value === "users")
      body = editing.value
        ? {
            name: form.name,
            role: form.role,
            active: form.active,
            ...(form.password ? { password: form.password } : {}),
          }
        : {
            name: form.name,
            email: form.email,
            role: form.role,
            password: form.password,
          };
    else if (tab.value === "profiles") {
      method = "POST";
      body = {
        name: form.name,
        chemistry: form.chemistry,
        nominal_mv: Number(form.nominal_mv),
        thresholds: Object.fromEntries(
          Object.entries(form.thresholds).map(([k, v]) => [k, Number(v)]),
        ),
      };
    } else
      body = {
        name: form.name,
        device_id: form.device_id,
        gateway_id: form.gateway_id,
      };
    if (editing.value) url += "/" + editing.value.id;
    const r: any = await $fetch(url, { method, body });
    const rotating = !!editing.value;
    if (r.provisioning) {
      provisioning.value = r.provisioning;
      provisioningHeading.value = rotating
        ? "Credenciales del gateway actualizadas"
        : "Gateway registrado";
    }
    reset();
    message.value = "Cambios guardados correctamente";
    // A refresh failure must not discard the only copy of a generated credential.
    try {
      await refresh();
    } catch {
      message.value =
        "Registro guardado. Actualiza la lista al cerrar los datos de conexión.";
    }
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
async function connection(row: any) {
  error.value = "";
  try {
    provisioning.value = await $fetch(
      "/api/gateways/" + row.id + "/connection",
    );
    provisioningHeading.value = "Datos de conexión del gateway";
  } catch (e) {
    error.value = errorText(e);
  }
}
async function moveToTrash(row: any) {
  pendingTrash.value = { ...row, resource: tab.value };
  error.value = "";
}
async function changeTrash(row: any, restore = false) {
  busy.value = true;
  error.value = "";
  message.value = "";
  try {
    await $fetch(
      `/api/${row.resource}/${row.id}/${restore ? "restore" : "trash"}`,
      { method: "POST" },
    );
    pendingTrash.value = null;
    reset();
    await refresh();
    message.value = restore
      ? "Registro restaurado"
      : "Registro enviado a papelera. Puedes restaurarlo desde la pestaña Papelera.";
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
</script>
<template>
  <section>
    <GatewayProvisioning
      v-if="provisioning"
      :bundle="provisioning"
      :heading="provisioningHeading"
      @close="provisioning = null"
    />
    <div class="page-heading">
      <div>
        <p class="eyebrow">CONFIGURACIÓN DE LA INSTALACIÓN</p>
        <h1>Administración</h1>
        <p class="muted">
          Organiza los dispositivos y define quién puede acceder.
        </p>
      </div>
    </div>
    <UTabs
      v-model="tab"
      :items="tabItems"
      :content="false"
      variant="link"
      class="page-tabs"
      :ui="{ list: 'overflow-x-auto', trigger: 'flex-none' }"
    />
    <UAlert
      v-if="error"
      class="page-alert"
      color="error"
      variant="subtle"
      icon="i-lucide-circle-x"
      :description="error"
      role="alert"
    />
    <UModal
      v-model:open="trashOpen"
      :title="`¿Enviar «${pendingTrash?.name || pendingTrash?.device_id}» a la papelera?`"
    >
      <template #body>
        <p class="modal-text">
          Dejará de aparecer en supervisión y planos. Sus datos e historial se
          conservarán y podrás restaurarlo. Retira primero sus dispositivos y
          ubicaciones dependientes.
        </p>
        <UAlert
          v-if="pendingTrash?.resource === 'gateways'"
          color="warning"
          variant="subtle"
          icon="i-lucide-triangle-alert"
          description="Este gateway dejará de aceptar datos en la aplicación y sus credenciales se retirarán del broker."
        />
      </template>
      <template #footer>
        <div class="modal-actions">
          <UButton
            color="neutral"
            variant="outline"
            label="Cancelar"
            :disabled="busy"
            @click="pendingTrash = null"
          />
          <UButton
            color="error"
            icon="i-lucide-trash-2"
            label="Enviar a papelera"
            :loading="busy"
            @click="changeTrash(pendingTrash)"
          />
        </div>
      </template>
    </UModal>
    <div
      v-if="data"
      :class="['audits', 'trash'].includes(tab) ? '' : 'admin-grid'"
    >
      <section class="panel">
        <div class="panel-heading">
          <h2>{{ tabs.find((t) => t[0] === tab)?.[1] }}</h2>
          <UButton
            v-if="!['audits', 'trash'].includes(tab)"
            color="neutral"
            variant="outline"
            icon="i-lucide-plus"
            label="Nuevo"
            @click="reset()"
          />
        </div>
        <div class="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Registro</th>
                <th>Detalle</th>
                <th></th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="row in data[tab]"
                :key="(row.resource || tab) + (row.version_id || row.id)"
              >
                <td>
                  <strong>{{
                    row.name || row.action || row.device_id
                  }}</strong><small>{{ row.email || row.id }}</small>
                </td>
                <td v-if="tab === 'profiles'">
                  {{ row.chemistry }} · {{ row.nominal_mv / 1000 }} V · versión
                  {{ row.version }}
                </td>
                <td v-else-if="tab === 'locations'">
                  {{
                    (
                      {
                        site: "Sede",
                        building: "Edificio",
                        level: "Nivel",
                      } as any
                    )[row.kind]
                  }}
                </td>
                <td v-else-if="tab === 'trash'">
                  {{
                    (
                      {
                        site: "Sede",
                        building: "Edificio",
                        level: "Nivel",
                        gateway: "Gateway",
                        lamp: "Lámpara",
                      } as any
                    )[row.kind]
                  }}
                  <small
                    >En papelera desde {{ dateText(row.deleted_at) }}</small
                  >
                </td>
                <td v-else-if="tab === 'gateways'">
                  <UBadge
                    :color="row.active ? 'success' : 'neutral'"
                    variant="subtle"
                    :icon="row.active ? 'i-lucide-circle-check' : 'i-lucide-circle-pause'"
                    :label="row.active ? 'Habilitado' : 'Desactivado'"
                  /><small>{{ dateText(row.last_seen) }}</small>
                </td>
                <td v-else-if="tab === 'users'">
                  {{ row.role === "admin" ? "Administrador" : "Monitor" }} ·
                  {{ row.active ? "Activo" : "Inactivo" }}
                </td>
                <td v-else-if="tab === 'audits'">
                  {{ row.actor || "Sistema"
                  }}<small>{{ dateText(row.created_at) }}</small>
                  <details>
                    <summary>Datos</summary>
                    <pre>{{ JSON.stringify(row.data, null, 2) }}</pre>
                  </details>
                </td>
                <td v-else><StatusPills :lamp="row" /></td>
                <td>
                  <div class="row-actions">
                    <UButton
                      v-if="tab === 'gateways'"
                      size="sm"
                      variant="ghost"
                      icon="i-lucide-plug"
                      label="Conexión"
                      @click="connection(row)"
                    />
                    <UButton
                      v-if="tab === 'lamps'"
                      :to="'/lamparas/' + row.id"
                      size="sm"
                      variant="ghost"
                      trailing-icon="i-lucide-arrow-right"
                      label="Asignar"
                    />
                    <UButton
                      v-else-if="!['audits', 'trash'].includes(tab)"
                      size="sm"
                      variant="ghost"
                      :icon="tab === 'profiles' ? 'i-lucide-copy-plus' : 'i-lucide-pencil'"
                      :label="tab === 'profiles' ? 'Nueva versión' : 'Editar'"
                      @click="reset(row)"
                    />
                    <UButton
                      v-if="['locations', 'gateways', 'lamps'].includes(tab)"
                      size="sm"
                      color="error"
                      variant="ghost"
                      icon="i-lucide-trash-2"
                      :aria-label="`Enviar ${row.name || row.device_id} a papelera`"
                      title="Enviar a papelera"
                      :disabled="busy"
                      @click="moveToTrash(row)"
                    />
                    <UButton
                      v-if="tab === 'trash'"
                      size="sm"
                      color="neutral"
                      variant="outline"
                      icon="i-lucide-rotate-ccw"
                      label="Restaurar"
                      :loading="busy"
                      @click="changeTrash(row, true)"
                    />
                  </div>
                </td>
              </tr>
              <tr v-if="!data[tab]?.length">
                <td colspan="3" class="empty">
                  {{
                    tab === "trash"
                      ? "La papelera está vacía"
                      : "Aún no hay registros"
                  }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </section>
      <form
        v-if="!['audits', 'trash'].includes(tab)"
        class="panel form-panel"
        @submit.prevent="submit"
      >
        <h2>
          {{
            editing
              ? tab === "profiles"
                ? "Crear nueva versión"
                : "Editar registro"
              : "Nuevo registro"
          }}
        </h2>
        <label
          >Nombre<input
            v-model="form.name"
            :disabled="tab === 'profiles' && !!editing"
            required
            maxlength="160" /></label
        ><template v-if="tab === 'locations'"
          ><label
            >Tipo<FfSelect
              v-model="form.kind"
              :items="[
                { value: 'site', label: 'Sede' },
                { value: 'building', label: 'Edificio' },
                { value: 'level', label: 'Nivel' },
              ]"
              :disabled="!!editing"
              @change="form.parent_id = ''"
            /></label
          ><label v-if="form.kind !== 'site'"
            >{{ form.kind === "building" ? "Sede" : "Edificio"
            }}<FfSelect
              v-model="form.parent_id"
              required
              empty-label="Selecciona"
              :items="
                data.locations
                  .filter(
                    (l: any) =>
                      l.kind === (form.kind === 'building' ? 'site' : 'building'),
                  )
                  .map((l: any) => ({ value: l.id, label: l.name }))
              "
            /></label
          ></template
        ><template v-if="tab === 'gateways'"
          ><label v-if="!editing"
            >Identificador MQTT<input
              v-model="form.id"
              pattern="[a-zA-Z0-9_-]{1,64}"
              required
              placeholder="gateway-edificio-a" /></label
          ><label
            >Ubicación<FfSelect
              v-model="form.location_id"
              empty-label="Sin ubicación"
              :items="data.locations.map((l: any) => ({ value: l.id, label: l.name }))"
            /></label
          ><label
            >Contraseña MQTT
            {{ editing ? "(vacía para conservar)" : "(vacía para generar)"
            }}<input
              v-model="form.password"
              type="password"
              minlength="12"
              autocomplete="new-password" /></label
          ><template v-if="editing"
            ><label
              >Intervalo de heartbeat (s)<input
                v-model.number="form.report_interval_s"
                type="number"
                min="5"
                max="86400" /></label
            ><label class="check"
              ><input v-model="form.active" type="checkbox" /> Gateway
              habilitado</label
            ></template
          >
          <p class="small muted">
            Las credenciales se sincronizan con Mosquitto en pocos segundos. No
            conceden acceso a otros gateways.
          </p></template
        ><template v-if="tab === 'users'"
          ><label v-if="!editing"
            >Correo<input
              v-model="form.email"
              type="email"
              required
              autocomplete="off" /></label
          ><label
            >Rol<FfSelect
              v-model="form.role"
              :items="[
                { value: 'monitor', label: 'Monitor' },
                { value: 'admin', label: 'Administrador' },
              ]"
            /></label
          ><label
            >{{
              editing
                ? "Restablecer contraseña (opcional)"
                : "Contraseña inicial"
            }}<input
              v-model="form.password"
              type="password"
              minlength="12"
              :required="!editing"
              autocomplete="new-password" /></label
          ><label v-if="editing" class="check"
            ><input v-model="form.active" type="checkbox" /> Usuario
            activo</label
          >
          <p class="small muted">
            Restablecer contraseña, desactivar o cambiar el rol revoca las
            sesiones existentes.
          </p></template
        ><template v-if="tab === 'profiles'"
          ><label
            >Química / tipo de batería<input
              v-model="form.chemistry"
              :disabled="!!editing"
              required
              placeholder="SLA / AGM" /></label
          ><label
            >Tensión nominal (mV)<input
              v-model.number="form.nominal_mv"
              type="number"
              min="1"
              max="65535"
              :disabled="!!editing"
              required
          /></label>
          <p class="notice">
            Valores iniciales de referencia del firmware de 6 V. Verifica cada
            valor según la batería concreta. Guardar no envía cambios a las
            lámparas.
          </p>
          <div class="grid-2">
            <label v-for="(label, key) in thresholdNames" :key="key"
              >{{ label }} (mV)<input
                v-model.number="form.thresholds[key]"
                type="number"
                min="1"
                max="65535"
                required
            /></label></div></template
        ><template v-if="tab === 'lamps'"
          ><label
            >Identificador del nodo<input
              v-model="form.device_id"
              required
              pattern="[a-zA-Z0-9_-]{1,64}" /></label
          ><label
            >Gateway<FfSelect
              v-model="form.gateway_id"
              required
              empty-label="Selecciona"
              :items="
                data.gateways
                  .filter((g: any) => g.active)
                  .map((g: any) => ({ value: g.id, label: g.name }))
              "
            /></label
          >
          <p class="muted">
            Las lámparas que publican telemetría se detectan automáticamente.
            Completa su ubicación desde la ficha.
          </p></template
        >
        <div class="form-actions">
          <UButton
            type="submit"
            icon="i-lucide-save"
            :loading="busy"
            :label="tab === 'profiles' ? 'Guardar versión' : 'Guardar'"
          />
          <UButton
            v-if="editing"
            type="button"
            color="neutral"
            variant="outline"
            label="Cancelar edición"
            @click="reset()"
          />
        </div>
      </form>
    </div>
  </section>
</template>
