<script setup lang="ts">
const model = defineModel<any>({ required: true });
const props = defineProps<{
  locations: any[];
  gateways: any[];
  historical?: boolean;
}>();
const buildings = computed(() =>
  props.locations.filter(
    (x) =>
      x.kind === "building" &&
      (!model.value.site || x.parent_id === model.value.site),
  ),
);
const levels = computed(() =>
  props.locations.filter(
    (x) =>
      x.kind === "level" &&
      (!model.value.building || x.parent_id === model.value.building),
  ),
);
</script>
<template>
  <div class="filter-bar">
    <label
      >Sede<select
        v-model="model.site"
        @change="
          model.building = '';
          model.level = '';
        "
      >
        <option value="">Todas las sedes</option>
        <option
          v-for="l in locations.filter((x) => x.kind === 'site')"
          :key="l.id"
          :value="l.id"
        >
          {{ l.name }}
        </option>
      </select></label
    ><label
      >Edificio<select v-model="model.building" @change="model.level = ''">
        <option value="">Todos los edificios</option>
        <option v-for="l in buildings" :key="l.id" :value="l.id">
          {{ l.name }}
        </option>
      </select></label
    ><label
      >Nivel<select v-model="model.level">
        <option value="">Todos los niveles</option>
        <option v-for="l in levels" :key="l.id" :value="l.id">
          {{ l.name }}
        </option>
      </select></label
    ><label
      >Gateway<select v-model="model.gateway">
        <option value="">Todos los gateways</option>
        <option v-for="g in gateways" :key="g.id" :value="g.id">
          {{ g.name }}
        </option>
      </select></label
    ><label
      >Estado<select v-model="model.state">
        <option value="">Todos los estados</option>
        <option value="alert">Con alerta</option>
        <option value="battery">En batería</option>
        <option value="testing">En prueba</option>
        <option v-if="!historical" value="offline">Sin comunicación</option>
        <option v-if="!historical" value="pending">Por asignar</option>
      </select></label
    >
  </div>
</template>
