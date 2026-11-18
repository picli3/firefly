// Iconos que usa Nuxt UI internamente (flechas, cierre, alertas...). Se empaquetan
// en el cliente para no depender de internet ni de un endpoint /api/_nuxt_icon,
// que chocaría con el catch-all de server/api/[...path].ts.
const uiIcons = [
  "arrow-down", "arrow-left", "arrow-right", "arrow-up", "arrow-up-right",
  "check", "chevron-down", "chevron-left", "chevron-right", "chevron-up",
  "chevrons-left", "chevrons-right", "circle-alert", "circle-check", "circle-x",
  "copy", "copy-check", "ellipsis", "eye", "eye-off", "info", "loader-circle",
  "menu", "minus", "panel-left-close", "panel-left-open", "plus", "search",
  "triangle-alert", "upload", "x",
].map((name) => `lucide:${name}`);

export default defineNuxtConfig({
  compatibilityDate: "2026-11-17",
  devtools: { enabled: false },
  modules: ["@nuxt/ui"],
  css: ["~/assets/main.css"],
  ui: {
    // Fuentes ya empaquetadas con @fontsource; sin descargas en la compilación.
    fonts: false,
    // El modo oscuro se habilitará en la fase 5, cuando todo use tokens.
    colorMode: false,
    theme: {
      colors: ["primary", "secondary", "success", "info", "warning", "error", "neutral"],
    },
  },
  icon: {
    provider: "none",
    fallbackToApi: false,
    clientBundle: { scan: true, icons: uiIcons },
  },
  app: {
    head: {
      title: "FireFly · Supervisión",
      link: [
        { rel: "icon", type: "image/svg+xml", href: "/branding/firefly.svg" },
      ],
      htmlAttrs: { lang: "es" },
      meta: [{ name: "robots", content: "noindex, nofollow" }],
    },
  },
  nitro: { externals: { inline: ["../../services"] } },
});
