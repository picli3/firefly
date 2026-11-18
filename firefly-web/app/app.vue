<script setup lang="ts">
const { user, admin, logout } = useSession();
const route = useRoute();
</script>
<template>
  <UApp :toaster="{ position: 'bottom-right' }">
  <div v-if="route.path === '/login'" class="login-shell"><NuxtPage /></div>
  <div v-else class="workspace">
    <aside class="sidebar">
      <NuxtLink to="/" class="brand"
        ><span class="brand-mark"
          ><img src="/branding/firefly.svg" alt="" aria-hidden="true"
        /></span>
        FireFly<span class="brand-tag">CONTROL</span></NuxtLink
      >
      <div class="nav-label">SUPERVISIÓN</div>
      <nav>
        <NuxtLink to="/">▦ <span>Vista general</span></NuxtLink
        ><NuxtLink to="/planos">⌖ <span>Planos y ubicaciones</span></NuxtLink
        ><NuxtLink to="/reportes">▤ <span>Reportes</span></NuxtLink
        ><NuxtLink v-if="admin" to="/admin"
          >⚙ <span>Administración</span></NuxtLink
        >
      </nav>
      <div class="sidebar-bottom">
        <span class="private-tag">● Entorno privado</span
        ><strong>{{ user?.name }}</strong
        ><small>{{ admin ? "Administrador" : "Monitor" }}</small
        ><button class="ghost" @click="logout">Cerrar sesión ↗</button>
      </div>
    </aside>
    <main class="main">
      <header class="topbar">
        <span>FireFly / Monitoreo de luminarias</span
        ><span class="timezone">America/Lima · Acceso autenticado</span>
      </header>
      <div class="content"><NuxtPage /></div>
      <footer>
        FireFly · Telemetría para mantenimiento · Las estimaciones no certifican
        autonomía.
      </footer>
    </main>
  </div>
  </UApp>
</template>
