export function mapAppearance(device, kind = "lamp", elapsedSeconds = 0) {
  const elapsed = Math.max(0, elapsedSeconds);
  let connected = !!device?.connected && Number.isFinite(device?.age_s);
  if (kind === "gateway" && device?.active === false) connected = false;
  if (kind === "lamp" && !Number.isFinite(device?.gateway_age_s))
    connected = false;
  if (connected && device.age_s !== null && device.age_s !== undefined)
    connected = device.age_s + elapsed <= 3 * (device.report_interval_s || 60);
  if (
    kind === "lamp" &&
    connected &&
    device.gateway_age_s !== null &&
    device.gateway_age_s !== undefined
  )
    connected =
      device.gateway_age_s + elapsed <= 3 * (device.gateway_interval || 60);
  return {
    connected,
    color: connected ? (kind === "gateway" ? "#316cad" : "#16785c") : "#85928c",
    label:
      kind === "gateway"
        ? connected
          ? "Conectado"
          : "Sin conexión"
        : connected
          ? "Activa"
          : "Inactiva",
  };
}
