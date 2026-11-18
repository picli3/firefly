<script setup lang="ts">
const props = defineProps<{ bundle: any; heading?: string }>();
const emit = defineEmits<{ close: [] }>();
const dialog = ref<HTMLDialogElement>();
const reveal = ref(false),
  showCommands = ref(false),
  feedback = ref(""),
  checking = ref(false);
const credentialStatus = ref(props.bundle.credential_status);
const broker = computed(() => props.bundle.broker);
const notice = computed(() =>
  credentialStatus.value === "verified"
    ? "Credenciales aceptadas por Mosquitto"
    : credentialStatus.value === "unknown"
      ? "Sin nueva verificación: contraseña no disponible"
      : "Credenciales todavía no verificadas en el broker",
);
onMounted(() => dialog.value?.showModal());
onBeforeUnmount(() => dialog.value?.close());
function close() {
  reveal.value = false;
  showCommands.value = false;
  emit("close");
}
async function copy(text: string) {
  try {
    await navigator.clipboard.writeText(text);
    feedback.value = "Copiado al portapapeles.";
  } catch {
    feedback.value =
      "El navegador no permitió copiar. Puedes seleccionar el texto manualmente.";
  }
}
function download(name: string, content: string, mime = "text/plain") {
  const url = URL.createObjectURL(new Blob([content], { type: mime }));
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function jsonText() {
  return JSON.stringify(
    {
      at_contract: broker.value.at_contract,
      at_supported_by_current_firmware: false,
      network_ready: broker.value.network_ready,
      warnings: broker.value.warnings,
      config: props.bundle.config,
    },
    null,
    2,
  );
}
async function verify() {
  checking.value = true;
  feedback.value = "";
  try {
    const result: any = await $fetch(
      "/api/gateways/" + props.bundle.gateway_id + "/verify",
      { method: "POST", body: { password: props.bundle.password } },
    );
    credentialStatus.value = result.credential_status;
    feedback.value =
      result.credential_status === "verified"
        ? "Conexión de autenticación verificada."
        : "El broker aún no aceptó estas credenciales. Revisa el worker y Mosquitto.";
  } catch (e) {
    feedback.value = errorText(e);
  } finally {
    checking.value = false;
  }
}
</script>
<template>
  <dialog
    ref="dialog"
    class="gateway-dialog"
    aria-labelledby="gateway-dialog-title"
    @close="close"
  >
    <div class="dialog-heading">
      <div>
        <p class="eyebrow">CONEXIÓN DEL GATEWAY</p>
        <h2 id="gateway-dialog-title">{{ heading || "Gateway registrado" }}</h2>
        <p class="muted">{{ bundle.gateway_name }} · {{ bundle.gateway_id }}</p>
      </div>
      <button
        type="button"
        class="secondary"
        aria-label="Cerrar datos del gateway"
        @click="close"
      >
        Cerrar ×
      </button>
    </div>
    <div class="dialog-content">
      <p
        class="pill"
        :class="credentialStatus === 'verified' ? 'good' : 'warn'"
      >
        {{ notice }}
      </p>
      <div class="notice">
        {{ bundle.at_notice }} Los datos MQTT sí corresponden a este registro;
        la implementación AT del C6 se hará después.
      </div>
      <div v-if="!broker.network_ready" class="error" role="status">
        <strong>La conexión desde la red del C6 aún no está preparada.</strong>
        <ul>
          <li v-for="warning in broker.warnings" :key="warning">
            {{ warning }}
          </li>
        </ul>
      </div>
      <section class="provision-section config-download">
        <h3>Configuración del C6 · archivo JSON</h3>
        <p class="small muted">
          Descarga {{ bundle.gateway_id }}-c6-config.json con la configuración
          MQTT completa: servidor, puerto, usuario, contraseña, certificado,
          identificación del gateway, red, reloj y topics.
        </p>
        <div v-if="bundle.config" class="actions">
          <button
            class="primary"
            @click="
              download(
                bundle.gateway_id + '-c6-config.json',
                jsonText(),
                'application/json',
              )
            "
          >
            Descargar configuración del C6 (.json)
          </button>
          <button class="secondary" @click="copy(jsonText())">
            Copiar JSON
          </button>
        </div>
        <p v-else class="notice">
          La configuración completa se entrega al crear el gateway o renovar su
          contraseña. No se puede recuperar la contraseña anterior.
        </p>
        <p v-if="bundle.config" class="small muted">
          Guarda este archivo antes de cerrar la ventana: contiene la contraseña
          de entrega única. La importación en el firmware C6 se implementará
          después.
        </p>
      </section>
      <div class="grid-2">
        <label class="span-2"
          >URL del broker<input
            :value="broker.uri || 'Sin configurar'"
            readonly
        /></label>
        <label>Servidor / host<input :value="broker.host" readonly /></label
        ><label>Puerto MQTT TLS<input :value="broker.port" readonly /></label>
        <label>Usuario MQTT<input :value="bundle.username" readonly /></label
        ><label
          >Identificador del cliente<input :value="bundle.client_id" readonly
        /></label>
        <label class="span-2"
          >Contraseña MQTT · entrega única<input
            v-if="bundle.password"
            :value="bundle.password"
            :type="reveal ? 'text' : 'password'"
            readonly
            autocomplete="off"
            spellcheck="false"
          /><span v-else class="muted"
            >No recuperable. Para generar un paquete completo, edita este
            gateway y establece una contraseña nueva.</span
          ></label
        >
      </div>
      <div v-if="bundle.password" class="actions">
        <button class="secondary" @click="reveal = !reveal">
          {{
            reveal ? "Ocultar contraseña" : "Mostrar contraseña"
          }}</button><button class="secondary" @click="copy(bundle.password)">
          Copiar contraseña</button
        ><button class="secondary" :disabled="checking" @click="verify">
          {{ checking ? "Verificando…" : "Verificar credenciales" }}
        </button>
      </div>
      <p v-if="bundle.password" class="small muted">
        La contraseña y los comandos completos solo se entregan en esta ventana.
        Guarda el paquete antes de cerrarla; el servidor conserva únicamente el
        hash. Cambiar la contraseña invalida la anterior para nuevas conexiones.
      </p>
      <section class="provision-section">
        <h3>Red, seguridad y topics</h3>
        <dl class="connection-details">
          <dt>Interfaz del C6</dt>
          <dd>Ethernet / DHCP</dd>
          <dt>Transporte</dt>
          <dd>MQTT 3.1.1 · TLS con validación de certificado · QoS 1</dd>
          <dt>Reloj / NTP</dt>
          <dd>{{ broker.ntp_servers.join(", ") }}</dd>
          <dt>Estado</dt>
          <dd class="mono">{{ bundle.topics.status }}</dd>
          <dt>Telemetría</dt>
          <dd class="mono">{{ bundle.topics.telemetry }}</dd>
          <dt>Reportes de pruebas</dt>
          <dd class="mono">{{ bundle.topics.test_report }}</dd>
          <dt>Confirmaciones</dt>
          <dd class="mono">{{ bundle.topics.configuration_ack }}</dd>
          <dt>Órdenes recibidas</dt>
          <dd class="mono">{{ bundle.topics.configuration_command }}</dd>
        </dl>
      </section>
      <section class="provision-section">
        <h3>Certificado de confianza del broker</h3>
        <p class="small muted">
          Cargar en el C6 el certificado PEM que corresponda al servidor, sin
          desactivar la verificación TLS. Incluido también en el paquete JSON.
        </p>
        <dl class="connection-details">
          <dt>Huella SHA-256</dt>
          <dd class="mono">
            {{ broker.ca_fingerprint_sha256 || "No disponible" }}
          </dd>
          <dt>Válido hasta</dt>
          <dd>{{ dateText(broker.ca_expires_at) }}</dd>
        </dl>
        <div class="actions">
          <button
            class="secondary"
            :disabled="!broker.ca_pem"
            @click="
              download(
                'firefly-broker-ca.pem',
                broker.ca_pem,
                'application/x-pem-file',
              )
            "
          >
            Descargar certificado PEM</button
          ><button
            class="secondary"
            :disabled="!broker.ca_pem"
            @click="copy(broker.ca_pem)"
          >
            Copiar certificado
          </button>
        </div>
      </section>
      <section class="provision-section">
        <h3>Contrato AT propuesto</h3>
        <p class="small muted">
          Incluye URL, puerto, usuario, contraseña, CA, red, reloj y topics. El
          bloque AT transporta el JSON en fragmentos base64 con SHA-256 para
          comprobar integridad; base64 no cifra las credenciales.
        </p>
        <template v-if="bundle.config"
          ><div class="actions">
            <button class="secondary" @click="showCommands = !showCommands">
              {{
                showCommands
                  ? "Ocultar comandos AT"
                  : "Mostrar comandos AT propuestos"
              }}
            </button>
          </div>
          <template v-if="showCommands"
            ><p class="notice">
              Contrato {{ broker.at_contract }}. El C6 actual no admite estos
              comandos. Este bloque contiene la contraseña.
            </p>
            <textarea
              class="at-commands mono"
              aria-label="Comandos AT propuestos para el futuro C6"
              :value="bundle.at_commands"
              readonly
              spellcheck="false"
            />
            <div class="actions">
              <button class="secondary" @click="copy(bundle.at_commands)">
                Copiar comandos AT</button
              ><button
                class="secondary"
                @click="
                  download(
                    bundle.gateway_id + '-AT-propuesto.txt',
                    '# CONTRATO AT PROPUESTO: NO COMPATIBLE CON EL FIRMWARE ACTUAL\r\n' +
                      bundle.at_commands +
                      '\r\n',
                  )
                "
              >
                Descargar AT propuesto
              </button>
            </div></template
          ></template
        >
        <p v-else class="error">
          No se puede generar un paquete completo sin contraseña, dirección
          válida y certificado que corresponda al host. Puedes consultar los
          datos de conexión y corregir la configuración indicada.
        </p>
      </section>
      <p v-if="feedback" class="notice" role="status">{{ feedback }}</p>
    </div>
    <div class="dialog-footer">
      <span class="small muted"
        >Las credenciales MQTT son independientes del acceso a la web.</span
      ><button class="primary" @click="close">Cerrar ventana</button>
    </div>
  </dialog>
</template>
<style scoped>
.gateway-dialog {
  width: min(860px, 94vw);
  max-height: 92vh;
  padding: 0;
  border: 1px solid var(--ff-border);
  border-radius: 14px;
  color: var(--ff-text);
  background: var(--ff-surface);
  box-shadow: 0 24px 100px #06291b45;
}
.gateway-dialog::backdrop {
  background: #09281eb0;
  backdrop-filter: blur(3px);
}
.dialog-heading,
.dialog-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 22px 26px;
  background: var(--ff-surface-subtle);
}
.dialog-heading {
  border-bottom: 1px solid var(--ff-border);
}
.dialog-heading p:last-child {
  margin: 8px 0 0;
}
.dialog-content {
  padding: 24px 26px;
}
.dialog-footer {
  border-top: 1px solid var(--ff-border);
}
.span-2 {
  grid-column: 1/-1;
}
.provision-section {
  margin-top: 28px;
  padding-top: 20px;
  border-top: 1px solid var(--ff-border);
}
.provision-section h3 {
  font-size: 15px;
}
.connection-details {
  display: grid;
  grid-template-columns: 160px minmax(0, 1fr);
  gap: 12px;
  font-size: 12px;
  margin-bottom: 20px;
}
.connection-details dt {
  color: var(--ff-text-muted);
}
.connection-details dd {
  margin: 0;
  overflow-wrap: anywhere;
}
.at-commands {
  width: 100%;
  height: 230px;
  resize: vertical;
  border: 1px solid var(--ff-border-strong);
  border-radius: 8px;
  background: var(--ff-surface-subtle);
  padding: 14px;
  font-size: 12px;
  color: var(--ff-text);
  margin: 12px 0;
}
.dialog-content > .actions {
  margin-top: 16px;
}
.error ul {
  padding-left: 18px;
  margin-bottom: 0;
}
@media (max-width: 600px) {
  .dialog-heading,
  .dialog-footer,
  .dialog-content {
    padding: 18px;
  }
  .connection-details {
    grid-template-columns: 1fr;
    gap: 5px;
  }
  .connection-details dd {
    margin-bottom: 10px;
  }
  .dialog-footer {
    align-items: start;
    flex-direction: column;
  }
  .dialog-content .grid-2 {
    grid-template-columns: 1fr;
  }
  .gateway-dialog {
    max-height: 94vh;
  }
}
</style>
