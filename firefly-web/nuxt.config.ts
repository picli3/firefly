export default defineNuxtConfig({
  compatibilityDate: "2026-11-17",
  devtools: { enabled: false },
  css: ["~/assets/main.css"],
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
