export default defineNuxtRouteMiddleware(async (to) => {
  const { user, refresh } = useSession();
  await refresh();
  if (!user.value && to.path !== "/login") return navigateTo("/login");
  if (user.value && to.path === "/login") return navigateTo("/");
  if (to.path === "/admin" && user.value?.role !== "admin")
    return navigateTo("/");
});
