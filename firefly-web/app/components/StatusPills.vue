<script setup lang="ts">
defineProps<{ lamp: any }>();
</script>
<template>
  <div class="pills">
    <span class="pill" :class="lamp.connected ? 'good' : 'neutral'">{{
      lamp.connected
        ? "En línea"
        : lamp.gateway_connected
          ? "Sin comunicación"
          : "Gateway sin conexión"
    }}</span><span
      v-if="lamp.latest"
      class="pill"
      :class="lamp.testing ? 'info' : lamp.ac ? 'neutral' : 'warn'"
      >{{
        lamp.testing ? "En prueba" : lamp.ac ? "AC presente" : "En batería"
      }}</span
    ><span v-if="lamp.critical" class="pill bad">Voltaje crítico</span
    ><span v-else-if="lamp.alert" class="pill warn">Alerta del nodo</span
    ><span v-if="lamp.latest && !lamp.sensor_valid" class="pill bad"
      >Sensor no válido</span
    ><span v-if="lamp.pending_assignment" class="pill neutral"
      >Por asignar</span
    >
  </div>
</template>
