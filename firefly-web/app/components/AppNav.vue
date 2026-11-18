<script setup lang="ts">
// Navegación principal: se usa en la barra lateral fija (escritorio) y en el
// cajón deslizante (tablet y móvil).
const { user, admin, logout } = useSession();
const links = computed(() => [
  { to: "/", label: "Vista general", icon: "i-lucide-layout-dashboard" },
  { to: "/planos", label: "Planos y ubicaciones", icon: "i-lucide-map" },
  { to: "/reportes", label: "Reportes", icon: "i-lucide-file-text" },
  ...(admin.value
    ? [{ to: "/admin", label: "Administración", icon: "i-lucide-settings" }]
    : []),
]);
</script>
<template>
  <NuxtLink to="/" class="brand"
    ><span class="brand-mark"
      ><img src="/branding/firefly.svg" alt="" aria-hidden="true"
    /></span>
    FireFly<span class="brand-tag">CONTROL</span></NuxtLink
  >
  <div class="nav-label">SUPERVISIÓN</div>
  <nav aria-label="Principal">
    <NuxtLink v-for="l in links" :key="l.to" :to="l.to">
      <UIcon :name="l.icon" class="nav-icon" />
      <span>{{ l.label }}</span>
    </NuxtLink>
  </nav>
  <div class="sidebar-bottom">
    <span class="private-tag"
      ><UIcon name="i-lucide-shield-check" class="nav-icon-sm" /> Entorno
      privado</span
    ><strong>{{ user?.name }}</strong
    ><small>{{ admin ? "Administrador" : "Monitor" }}</small
    ><button class="ghost" @click="logout">
      <UIcon name="i-lucide-log-out" class="nav-icon-sm" /> Cerrar sesión
    </button>
  </div>
</template>
