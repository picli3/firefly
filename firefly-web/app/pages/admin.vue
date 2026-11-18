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
    <div class="tabs">
      <button
        v-for="t in tabs"
        :key="t[0]"
        :class="{ selected: tab === t[0] }"
        @click="tab = t[0]"
      >
        {{ t[1] }}
      </button>
    </div>
    <p v-if="error" class="error" role="alert">{{ error }}</p>
    <p v-if="message" class="success wrap" role="status">{{ message }}</p>
    <section
      v-if="pendingTrash"
      class="panel"
      role="alertdialog"
      aria-labelledby="trash-confirm-title"
    >
      <h2 id="trash-confirm-title">
        ¿Enviar «{{ pendingTrash.name || pendingTrash.device_id }}» a la
        papelera?
      </h2>
      <p>
        Dejará de aparecer en supervisión y planos. Sus datos e historial se
        conservarán y podrás restaurarlo. Retira primero sus dispositivos y
        ubicaciones dependientes.
      </p>
      <p v-if="pendingTrash.resource === 'gateways'">
        Este gateway dejará de aceptar datos en la aplicación y sus credenciales
        se retirarán del broker.
      </p>
      <div class="actions">
        <button
          class="primary"
          :disabled="busy"
          @click="changeTrash(pendingTrash)"
        >
          Confirmar envío a papelera
        </button>
        <button class="secondary" :disabled="busy" @click="pendingTrash = null">
          Cancelar
        </button>
      </div>
    </section>
    <div
      v-if="data"
      :class="['audits', 'trash'].includes(tab) ? '' : 'admin-grid'"
    >
      <section class="panel">
        <div class="panel-heading">
          <h2>{{ tabs.find((t) => t[0] === tab)?.[1] }}</h2>
          <button
            v-if="!['audits', 'trash'].includes(tab)"
            class="secondary"
            @click="reset()"
          >
            + Nuevo
          </button>
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
                  <span class="pill" :class="row.active ? 'good' : 'neutral'">{{
                    row.active ? "Habilitado" : "Desactivado"
                  }}</span><small>{{ dateText(row.last_seen) }}</small>
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
                  <button
                    v-if="tab === 'gateways'"
                    type="button"
                    class="text-link"
                    style="margin-right: 12px"
                    @click="connection(row)"
                  >
                    Conexión
                  </button>
                  <NuxtLink
                    v-if="tab === 'lamps'"
                    :to="'/lamparas/' + row.id"
                    class="text-link"
                    >Asignar →</NuxtLink
                  ><button
                    v-else-if="!['audits', 'trash'].includes(tab)"
                    class="text-link"
                    @click="reset(row)"
                  >
                    {{ tab === "profiles" ? "Nueva versión" : "Editar" }}
                  </button>
                  <button
                    v-if="['locations', 'gateways', 'lamps'].includes(tab)"
                    class="text-link"
                    style="margin-left: 12px"
                    :disabled="busy"
                    @click="moveToTrash(row)"
                  >
                    Enviar a papelera
                  </button>
                  <button
                    v-if="tab === 'trash'"
                    class="secondary"
                    :disabled="busy"
                    @click="changeTrash(row, true)"
                  >
                    Restaurar
                  </button>
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
            >Tipo<select
              v-model="form.kind"
              :disabled="!!editing"
              @change="form.parent_id = ''"
            >
              <option value="site">Sede</option>
              <option value="building">Edificio</option>
              <option value="level">Nivel</option>
            </select></label
          ><label v-if="form.kind !== 'site'"
            >{{ form.kind === "building" ? "Sede" : "Edificio"
            }}<select v-model="form.parent_id" required>
              <option value="">Selecciona</option>
              <option
                v-for="l in data.locations.filter(
                  (l: any) =>
                    l.kind === (form.kind === 'building' ? 'site' : 'building'),
                )"
                :key="l.id"
                :value="l.id"
              >
                {{ l.name }}
              </option>
            </select></label
          ></template
        ><template v-if="tab === 'gateways'"
          ><label v-if="!editing"
            >Identificador MQTT<input
              v-model="form.id"
              pattern="[a-zA-Z0-9_-]{1,64}"
              required
              placeholder="gateway-edificio-a" /></label
          ><label
            >Ubicación<select v-model="form.location_id">
              <option value="">Sin ubicación</option>
              <option v-for="l in data.locations" :key="l.id" :value="l.id">
                {{ l.name }}
              </option>
            </select></label
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
            >Rol<select v-model="form.role">
              <option value="monitor">Monitor</option>
              <option value="admin">Administrador</option>
            </select></label
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
            >Gateway<select v-model="form.gateway_id" required>
              <option value="">Selecciona</option>
              <option
                v-for="g in data.gateways.filter((g: any) => g.active)"
                :key="g.id"
                :value="g.id"
              >
                {{ g.name }}
              </option>
            </select></label
          >
          <p class="muted">
            Las lámparas que publican telemetría se detectan automáticamente.
            Completa su ubicación desde la ficha.
          </p></template
        ><button class="primary" :disabled="busy">
          {{ tab === "profiles" ? "Guardar versión" : "Guardar" }}</button
        ><button
          v-if="editing"
          type="button"
          class="secondary"
          @click="reset()"
        >
          Cancelar edición
        </button>
      </form>
    </div>
  </section>
</template>
