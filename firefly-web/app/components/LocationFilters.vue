<script setup lang="ts">
const model = defineModel<any>({ required: true });
const props = defineProps<{
  locations: any[];
  gateways: any[];
  historical?: boolean;
}>();
const opt = (l: any) => ({ value: l.id, label: l.name });
const sites = computed(() =>
  props.locations.filter((x) => x.kind === "site").map(opt),
);
const buildings = computed(() =>
  props.locations
    .filter(
      (x) =>
        x.kind === "building" &&
        (!model.value.site || x.parent_id === model.value.site),
    )
    .map(opt),
);
const levels = computed(() =>
  props.locations
    .filter(
      (x) =>
        x.kind === "level" &&
        (!model.value.building || x.parent_id === model.value.building),
    )
    .map(opt),
);
const gatewayItems = computed(() => props.gateways.map(opt));
const states = computed(() => [
  { value: "alert", label: "Con alerta" },
  { value: "battery", label: "En batería" },
  { value: "testing", label: "En prueba" },
  ...(props.historical
    ? []
    : [
        { value: "online", label: "En línea" },
        { value: "offline", label: "Sin comunicación" },
        { value: "pending", label: "Por asignar" },
      ]),
]);
</script>
<template>
  <div class="filter-bar">
    <label
      >Sede<FfSelect
        v-model="model.site"
        empty-label="Todas las sedes"
        :items="sites"
        @change="
          model.building = '';
          model.level = '';
        "
    /></label>
    <label
      >Edificio<FfSelect
        v-model="model.building"
        empty-label="Todos los edificios"
        :items="buildings"
        @change="model.level = ''"
    /></label>
    <label
      >Nivel<FfSelect
        v-model="model.level"
        empty-label="Todos los niveles"
        :items="levels"
    /></label>
    <label
      >Gateway<FfSelect
        v-model="model.gateway"
        empty-label="Todos los gateways"
        :items="gatewayItems"
    /></label>
    <label
      >Estado<FfSelect
        v-model="model.state"
        empty-label="Todos los estados"
        :items="states"
    /></label>
  </div>
</template>
