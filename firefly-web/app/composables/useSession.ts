export function useSession() {
  const user = useState<any>("session-user", () => null);
  const fetcher = useRequestFetch();
  async function refresh() {
    try {
      user.value = await fetcher("/api/auth/me");
    } catch {
      user.value = null;
    }
  }
  async function logout() {
    await $fetch("/api/auth/logout", { method: "POST" });
    user.value = null;
    await navigateTo("/login");
  }
  return {
    user,
    refresh,
    logout,
    admin: computed(() => user.value?.role === "admin"),
  };
}
export function errorText(e: any): string {
  return (
    e?.data?.statusMessage ||
    e?.statusMessage ||
    e?.message ||
    "No se pudo completar la operación"
  );
}
export function dateText(value: any): string {
  return value
    ? new Date(value).toLocaleString("es-PE", { timeZone: "America/Lima" })
    : "Sin datos";
}
