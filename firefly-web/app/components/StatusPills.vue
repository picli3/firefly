<script setup lang="ts">
// Estado de una lámpara: color + icono + texto (nunca solo color).
const props = defineProps<{ lamp: any }>();
type Color = "success" | "info" | "warning" | "error" | "neutral";
const items = computed(() => {
  const l = props.lamp,
    out: { label: string; color: Color; icon: string }[] = [];
  out.push(
    l.connected
      ? { label: "En línea", color: "success", icon: "i-lucide-wifi" }
      : l.gateway_connected
        ? { label: "Sin comunicación", color: "neutral", icon: "i-lucide-wifi-off" }
        : { label: "Gateway sin conexión", color: "neutral", icon: "i-lucide-router" },
  );
  if (l.latest)
    out.push(
      l.testing
        ? { label: "En prueba", color: "info", icon: "i-lucide-flask-conical" }
        : l.ac
          ? { label: "AC presente", color: "neutral", icon: "i-lucide-plug-zap" }
          : { label: "En batería", color: "warning", icon: "i-lucide-battery-medium" },
    );
  if (l.critical)
    out.push({ label: "Voltaje crítico", color: "error", icon: "i-lucide-triangle-alert" });
  else if (l.alert)
    out.push({ label: "Alerta del nodo", color: "warning", icon: "i-lucide-bell-ring" });
  if (l.latest && !l.sensor_valid)
    out.push({ label: "Sensor no válido", color: "error", icon: "i-lucide-thermometer" });
  if (l.pending_assignment)
    out.push({ label: "Por asignar", color: "neutral", icon: "i-lucide-map-pin-off" });
  return out;
});
</script>
<template>
  <div class="pills">
    <UBadge
      v-for="i in items"
      :key="i.label"
      :color="i.color"
      variant="subtle"
      :icon="i.icon"
      :label="i.label"
      size="sm"
    />
  </div>
</template>
