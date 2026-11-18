// Deterministic synthetic data for visual QA, never production telemetry.
export function monthlyFixture() {
  const measurements = [],
    tests = [];
  for (let month = 0; month < 11; month++)
    for (let device = 0; device < 2; device++) {
      const voltage = device ? 6300 : 4300;
      for (let i = 0; i < 4; i++) {
        const date = new Date(Date.UTC(2026, month, 5 + i, 15));
        measurements.push({
          lamp_id: "demo-" + device,
          name: "DEMO · Lámpara " + (device ? "6 V" : "4 V"),
          device_id: "sim-" + device,
          gateway_id: "gateway-demo",
          received_at: date,
          measured_at: date,
          config_version: 1,
          profile_snapshot: {
            name: "Perfil sintético " + (device ? "6 V" : "4 V"),
          },
          location_snapshot: {
            site: "DEMOSTRACIÓN - datos sintéticos",
            building: "Edificio de ejemplo",
            level: "Nivel 1",
            place: device ? "Salida principal" : "Pasillo norte",
          },
          data: {
            voltage_mv: voltage - month * 8 - i * 10,
            current_ma: 320 + i * 5,
            temperature_centi_c: 2500 + month * 9 + i * 5,
            flags: 8,
          },
        });
      }
      if (!device)
        for (let j = 0; j < 3; j++) {
          const date = new Date(Date.UTC(2026, month, 12 + j, 15));
          tests.push({
            ...measurements.at(-1),
            received_at: date,
            measured_at: date,
            data: {
              test_id: month * 3 + j,
              result: j === 2 && month % 3 !== 0 ? "LOW_VOLTAGE" : "PASS",
              duration_s: 30,
              voltage_min_mv: voltage - 250,
              flags: 9,
            },
          });
        }
    }
  return {
    measurements,
    tests,
    filters: { from: "2026-01-01T05:00:00Z", to: "2026-11-17T18:00:00Z" },
    context: {
      preparedBy: "Demostración - datos sintéticos",
      scope: "DEMOSTRACIÓN - datos sintéticos / Edificio de ejemplo",
      generatedAt: "2026-11-17T18:00:00Z",
    },
  };
}
