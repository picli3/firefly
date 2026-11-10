# Protocolos de telemetria

## Zigbee

- Perfil: Home Automation (`0x0104`).
- Endpoint: `1` en nodo y coordinador.
- Cluster privado: `0xFC00`.
- Comando: `0x01` (`telemetry`).
- Comando hacia el nodo: `0x02` (`start_battery_test`).
- Comando hacia el nodo: `0x03` (`schedule_battery_test`).
- Comando hacia el coordinador: `0x04` (`battery_test_report`).
- Telemetria y reportes: servidor (nodo) a cliente (coordinador `0x0000`).
- Agenda y prueba inmediata: cliente (coordinador) a servidor (nodo), unicast.

La carga util Zigbee es `emergency_telemetry_t`, empacada en little-endian.

## UART H2 a S3

Configuracion: 115200 baud, 8 data bits, sin paridad y 1 stop bit.

```text
SOF       version  type       length     payload       CRC16
A5 5A     01       01         LL HH      N bytes       CC CC
```

- `length` es little-endian y no puede superar 128 bytes.
- El CRC16/CCITT-FALSE usa polinomio `0x1021` e inicial `0xFFFF`.
- El CRC cubre `version`, `type`, `length` y `payload`; no cubre el SOF.
- `type 0x01` contiene `emergency_telemetry_t`.

El comando Zigbee `0x02` contiene un `uint16_t` little-endian con la duracion de
una prueba QA inmediata. El nodo acepta entre 1 y 300 segundos; cero cancela una
prueba activa. El boton local inicia la misma prueba con una duracion de 30 s.

El comando `0x03` programa una prueba automatica en un nodo concreto:

| Campo | Tipo | Descripcion |
|---|---|---|
| `version` | `uint8_t` | Version, actualmente 1 |
| `group_index` | `uint8_t` | Grupo asignado por el coordinador |
| `test_id` | `uint16_t` | Identificador de campana |
| `start_delay_s` | `uint32_t` | Retardo relativo antes de abrir el rele |
| `duration_s` | `uint16_t` | Duracion bajo bateria, 300 s en el perfil horario actual del C6 |
| `reserved` | `uint16_t` | Reservado, cero |

El H2 persiste la agenda en NVS, la consume antes de iniciar y mantiene un timeout
local del rele. Durante la prueba mide cada 5 s. Al finalizar envia `0x04`:

| Campo | Tipo | Descripcion |
|---|---|---|
| `version` | `uint8_t` | Version, actualmente 1 |
| `result` | `uint8_t` | Resultado enumerado |
| `test_id` | `uint16_t` | Campana correspondiente; cero indica QA manual |
| `device_id` | `uint32_t` | Identificador del nodo |
| `voltage_before_mv` | `uint16_t` | Tension antes de abrir el rele |
| `voltage_min_mv` | `uint16_t` | Minimo observado bajo carga |
| `current_avg_ma` | `int16_t` | Corriente promedio |
| `current_peak_ma` | `int16_t` | Magnitud maxima de corriente |
| `power_avg_mw` | `int32_t` | Potencia promedio |
| `temperature_max_centi_c` | `int16_t` | Temperatura maxima |
| `duration_s` | `uint16_t` | Duracion programada |
| `sample_count` | `uint8_t` | Muestras validas acumuladas |
| `flags` | `uint8_t` | Ultimos flags de telemetria |

Resultados: `0 PASS`, `1 LOW_VOLTAGE`, `2 LOW_CURRENT`, `3 HIGH_TEMPERATURE`,
`4 AC_NOT_PRESENT`, `5 SENSOR_INVALID` y `6 ABORTED`. Si el reporte no puede
enviarse, el H2 conserva uno pendiente en NVS y reintenta cuando vuelve Zigbee.

## Payload v1

| Campo | Tipo | Unidad |
|---|---|---|
| `payload_version` | `uint8_t` | Version, actualmente 1 |
| `flags` | `uint8_t` | bit0 AC, bit1 alerta, bit2 UVLO, bit3 sensor valido, bit4 prueba activa |
| `sequence` | `uint16_t` | Contador de muestras |
| `device_id` | `uint32_t` | Identificador derivado de la MAC |
| `vpack_mv` | `uint16_t` | mV |
| `ipack_ma` | `int16_t` | mA; positivo descarga, negativo carga |
| `power_mw` | `int32_t` | mW |
| `temp_centi_c` | `int16_t` | centesimas de grado Celsius |
| `soh_pct` | `uint8_t` | 0 a 100 % |
| `reserved` | `uint8_t` | Reservado, cero |
| `uptime_s` | `uint32_t` | segundos desde arranque |
