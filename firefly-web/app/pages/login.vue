<script setup lang="ts">
const email = ref(""),
  password = ref(""),
  error = ref(""),
  busy = ref(false);
const { refresh } = useSession();
async function login() {
  busy.value = true;
  error.value = "";
  try {
    await $fetch("/api/auth/login", {
      method: "POST",
      body: { email: email.value, password: password.value },
    });
    await refresh();
    await navigateTo("/");
  } catch (e) {
    error.value = errorText(e);
  } finally {
    busy.value = false;
  }
}
</script>
<template>
  <div class="login-card">
    <div class="brand login-brand">
      <span class="brand-mark"
        ><img src="/branding/firefly.svg" alt="" aria-hidden="true"
      /></span>
      FireFly
    </div>
    <p class="eyebrow">SUPERVISIÓN DE LUMINARIAS</p>
    <h1>Acceso al sistema</h1>
    <p class="muted">
      Inicia sesión para consultar tus dispositivos, planos y reportes.
    </p>
    <form @submit.prevent="login">
      <label
        >Correo electrónico<input
          v-model="email"
          type="email"
          autocomplete="username"
          required /></label
      ><label
        >Contraseña<input
          v-model="password"
          type="password"
          autocomplete="current-password"
          required
      /></label>
      <p v-if="error" class="error" role="alert">{{ error }}</p>
      <button class="primary full" :disabled="busy">
        {{ busy ? "Ingresando…" : "Iniciar sesión →" }}
      </button>
    </form>
    <p class="small muted">
      Acceso privado. Solicita tu cuenta al administrador de la instalación.
    </p>
  </div>
</template>
