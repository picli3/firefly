import { defineEventHandler, setHeader } from "h3";
export default defineEventHandler((event) => {
  setHeader(event, "X-Frame-Options", "DENY");
  setHeader(event, "Referrer-Policy", "same-origin");
  setHeader(event, "X-Content-Type-Options", "nosniff");
});
