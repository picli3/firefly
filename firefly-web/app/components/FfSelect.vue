<script setup lang="ts">
// Selector FireFly sobre USelectMenu de Nuxt UI, con la misma semántica que el
// <select> nativo al que sustituye:
// - el valor vacío ("") se representa con `emptyLabel` ("Todas las sedes"...);
// - `required` sigue bloqueando el envío del formulario (input proxy invisible);
// - emite `change` solo cuando el usuario cambia el valor;
// - con más de 7 opciones muestra un buscador.
export interface FfSelectItem {
  value: string;
  label: string;
  disabled?: boolean;
}
const model = defineModel<string>({ default: "" });
const props = defineProps<{
  items: FfSelectItem[];
  emptyLabel?: string;
  placeholder?: string;
  required?: boolean;
  disabled?: boolean;
}>();
const emit = defineEmits<{ change: [value: string] }>();
// Reka UI no admite "" como valor de opción; se usa un centinela interno.
const NONE = "__ff_none__";
const options = computed<FfSelectItem[]>(() =>
  props.emptyLabel !== undefined
    ? [{ value: NONE, label: props.emptyLabel }, ...props.items]
    : props.items,
);
const inner = computed<string | undefined>({
  get: () =>
    model.value === "" || model.value === null || model.value === undefined
      ? props.emptyLabel !== undefined
        ? NONE
        : undefined
      : model.value,
  set: (v) => {
    const next = !v || v === NONE ? "" : v;
    if (next === model.value) return;
    model.value = next;
    emit("change", next);
  },
});
</script>
<template>
  <div class="ff-select">
    <USelectMenu
      v-model="inner"
      :items="options"
      value-key="value"
      label-key="label"
      :placeholder="placeholder || emptyLabel || 'Selecciona'"
      :disabled="disabled"
      :search-input="options.length > 7 ? { placeholder: 'Buscar…' } : false"
      size="lg"
      class="w-full ff-select-trigger"
    />
    <input
      v-if="required"
      class="ff-select-proxy"
      :value="model"
      required
      tabindex="-1"
      aria-hidden="true"
    />
  </div>
</template>
