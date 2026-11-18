<script setup lang="ts">
const route = useRoute();
const menuOpen = ref(false);
watch(
  () => route.fullPath,
  () => (menuOpen.value = false),
);
</script>
<template>
  <UApp :toaster="{ position: 'bottom-right' }">
    <div v-if="route.path === '/login'" class="login-shell"><NuxtPage /></div>
    <div v-else class="workspace">
      <aside class="sidebar"><AppNav /></aside>
      <USlideover
        v-model:open="menuOpen"
        side="left"
        title="Menú"
        :ui="{ content: 'ff-drawer max-w-[290px]', header: 'sr-only' }"
      >
        <template #body><div class="ff-drawer-body"><AppNav /></div></template>
      </USlideover>
      <main class="main">
        <header class="topbar">
          <div class="topbar-start">
            <UButton
              class="menu-button"
              icon="i-lucide-menu"
              color="neutral"
              variant="ghost"
              aria-label="Abrir menú"
              @click="menuOpen = true"
            />
            <span>FireFly / Monitoreo de luminarias</span>
          </div>
          <div class="topbar-end">
            <span class="timezone">America/Lima · Acceso autenticado</span>
            <UColorModeButton
              color="neutral"
              variant="ghost"
              aria-label="Cambiar entre modo claro y oscuro"
            />
          </div>
        </header>
        <div class="content"><NuxtPage /></div>
        <footer>
          FireFly · Telemetría para mantenimiento · Las estimaciones no
          certifican autonomía.
        </footer>
      </main>
    </div>
  </UApp>
</template>
