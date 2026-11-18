# Protocolos de telemetria

## Zigbee

- Perfil: Home Automation (`0x0104`).
- Endpoint: `1` en nodo y coordinador.
- Cluster privado: `0xFC00`.
- Comando: `0x01` (`telemetry`).
- Comando hacia el nodo: `0x02` (`start_battery_test`).
- Comando hacia el nodo: `0x03` (`schedule_battery_test`).
- Comando hacia el coordinador: `0x04` (`battery_test_report`).
- Comando hacia el nodo: `0x05` (`battery_config`), version 1.
- Comando hacia el coordinador: `0x06` (`battery_config_ack`), version 1.
- Telemetria y reportes: servidor (nodo) a cliente (coordinador `0x0000`).
- Agenda y prueba inmediata: cliente (coordinador) a servidor (nodo), unicast.

Las cargas utiles son estructuras empacadas little-endian. El C6 acepta v1 y
v2; el H2 configurable emite v2. Ningun byte reservado de v1 cambia de significado.

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

### Solicitud individual MQTT del C6 (sin cambios del formato Zigbee)

`firefly/v1/gateways/<gateway>/commands/test` recibe `{schema_version:1,command_id:UUID,device_id:hex8,test_id:32768..65535,duration_s:30,issued_at_ms,expires_at_ms}`. QoS1, no retain, vencimiento de 30 segundos. `test-ack` responde `accepted`, `rejected` o `unknown` con UUID, nodo e ID; aceptar indica envío Zigbee, no PASS. `test-report` conserva el resultado H2 y añade `command_id` opcional para correlacionarlo. El C6 aplica `SCHEDULE_TEST` existente con demora cero y duración 30 s; sus campañas automáticas usan IDs 1–32767. No cambian las estructuras comunes ni los consumidores H2-coordinator/S3.

La capacidad `test_capable` en `status` requiere firmware receptor y reloj válido. UUID/marca temporal persistidos antes del envío impiden repetir órdenes por duplicados o reinicios del C6. El programa ya transferido sigue sujeto a la persistencia del H2. Contrato detallado y limitaciones en `firefly-web/plan/pruebas-individuales.md`.

## Configuracion de bateria y tramas v2 (FW-001)

`emergency_telemetry_v2_t` ocupa 33 bytes: los 24 de telemetria v1, con
`payload_version=2`, seguidos de `config_version:uint32`,
`config_capable:uint8` y `boot_id:uint32`. `battery_test_report_v2_t` ocupa
35 bytes: los 26 del reporte v1, con `version=2`, y el mismo sufijo de 9 bytes.
Los prefijos conservan campos/unidades, incluido `reserved=0` de telemetria.
Solo se aceptan parejas exactas de longitud/version. La revision del reporte
es la capturada al iniciar la prueba, no la activa al transmitirlo.
`config_capable=1` indica soporte, `0` desconocido; revision cero significa
perfil heredado no confirmado. Boot ID es aleatorio por arranque del H2,
no una fecha civil. Los comandos de prueba `0x02` y `0x03` no cambian.

### `0x05`: configuracion completa, 35 bytes

| Offset | Campo | Tipo |
|---:|---|---|
| 0 | version = 1 | uint8 |
| 1 | command_uuid | 16 octetos RFC4122, orden de representacion del UUID |
| 17 | revision por lampara (>0) | uint32 LE |
| 21 | attention_mv | uint16 LE |
| 23 | alert_mv | uint16 LE |
| 25 | critical_mv | uint16 LE |
| 27 | test_min_mv | uint16 LE |
| 29 | full_mv | uint16 LE |
| 31 | ac_empty_mv | uint16 LE |
| 33 | dc_empty_mv | uint16 LE |

Todos los umbrales son mV positivos hasta 65535; deben satisfacer
`critical <= alert < attention <= full`, `ac_empty < full`, `dc_empty < full`
y `test_min <= full`. El H2 guarda el conjunto completo antes de aplicar;
durante prueba lo guarda pendiente. Una revision pendiente mayor sustituye
la anterior. Duplicados exactos no reescriben NVS; regresiones y conflictos
se rechazan. Revision uint32 no se permite desbordar ni volver a cero.

### `0x06`: respuesta real del H2, 27 bytes

| Offset | Campo | Tipo |
|---:|---|---|
| 0 | version = 1 | uint8 |
| 1 | command_uuid original | 16 octetos RFC4122 |
| 17 | device_id del H2 | uint32 LE |
| 21 | revision solicitada | uint32 LE |
| 25 | status: 0 applied, 1 deferred, 2 rejected | uint8 |
| 26 | reason: 0 none, 1 invalid, 2 stale, 3 conflict, 4 storage | uint8 |

El H2 reanuncia activo/pendiente cada 30 s; `applied` exige commit correcto y
aplicacion en RAM. `deferred` exige pendiente persistido. El C6 valida nodo,
direccion y capacidad antes de publicar a la web. No crea confirmaciones a
partir de su envio de la solicitud.

### MQTT existente de FireFly Web

Raiz `firefly/v1/gateways/<gateway_id>`, QoS1, mensajes no retenidos:

- `/commands/config`: `{schema_version:1, command_id:UUID, device_id:hex8,
  version:uint32_positivo, thresholds:{attention_mv,alert_mv,critical_mv,
  test_min_mv,full_mv,ac_empty_mv,dc_empty_mv}}`.
- `/config-ack`: `{schema_version:1,message_id,command_id,device_id,version,
  status:"applied"|"deferred"|"rejected",reason}`. UUID canonico en minusculas.
- `/telemetry`: capacidad real por nodo, `config_version` opcional y boot ID
  nativo para v2. V1 mantiene capacidad falsa y revision ausente.
- `/test-report`: revision opcional del inicio y boot ID original; no agrega
  `config_capable` porque el esquema web de resultados no admite ese campo.

Revision cero se omite en MQTT; no significa perfil confirmado. No se exigen
quimica/voltaje nominal ni se cambia el contrato web. CSV mantiene sus columnas
anteriores y no almacena estos metadatos; consultar MQTT para la revision.

El contrato UART H2/S3 documentado sigue siendo v1; esos proyectos historicos
no estan presentes en este checkout y no se han compilado ni migrado a v2.
