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
export function agoText(seconds: number | null | undefined): string {
  if (seconds === null || seconds === undefined) return "";
  const s = Math.max(0, Math.floor(seconds));
  if (s < 60) return `Hace ${s} s`;
  if (s < 3600) return `Hace ${Math.floor(s / 60)} min`;
  if (s < 86400) return `Hace ${Math.floor(s / 3600)} h`;
  return `Hace ${Math.floor(s / 86400)} d`;
}
export function dateText(value: any): string {
  return value
    ? new Date(value).toLocaleString("es-PE", { timeZone: "America/Lima" })
    : "Sin datos";
}
