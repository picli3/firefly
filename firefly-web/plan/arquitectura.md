# Arquitectura y contrato MQTT v1

Nuxt/Nitro (API y UI) → PostgreSQL. Worker Node suscrito a Mosquitto; outbox persistente para configuración. SSE autenticado consulta estado persistente. Caddy termina HTTPS; servicios internos no se exponen salvo MQTT TLS.

Entidades: usuarios/sesiones, ubicaciones jerárquicas, gateways, lámparas, perfiles/versiones, órdenes, muestras, resultados, planos y auditoría. UTC en almacenamiento y America/Lima en presentación.

## Contrato propuesto para firmware y simulador

Raíz `firefly/v1/gateways/{gateway_id}`. Gateway autentica con usuario igual a gateway_id y solo accede a su raíz.

- `/status`: `{schema_version:1,message_id,online,boot_id}`; retained y LWT offline.
- `/telemetry`: `{schema_version:1,message_id,device_id,boot_id,sequence,uptime_s,measured_at?,live,report_interval_s,voltage_mv,current_ma,power_mw,temperature_centi_c,soh_pct,flags,config_version?,config_capable}`. `measured_at` solo si reloj sincronizado; `live:false` para retransmisión.
- `/test-report`: mismos identificadores y tiempo, `test_id,result,voltage_before_mv,voltage_min_mv,current_avg_ma,current_peak_ma,power_avg_mw,temperature_max_centi_c,duration_s,sample_count,flags,config_version?`. Resultados originales del protocolo Zigbee.
- `/commands/config`: `{schema_version:1,command_id,device_id,version,thresholds}`. QoS1, no retained. Identificador y versión idénticos en reintentos.
- `/config-ack`: `{schema_version:1,message_id,command_id,device_id,version,status,reason?}`. Status `applied`, `rejected` o `deferred`; applied solo tras persistencia y aplicación en H2.

`thresholds`: attention_mv, alert_mv, critical_mv, test_min_mv, full_mv, ac_empty_mv, dc_empty_mv. Validar rangos enteros positivos y crítico ≤ alerta < atención ≤ lleno, vacíos < lleno. El firmware debe aplicar el conjunto atómicamente y calcular rangos en vez de recibir divisores independientes.

Deduplicar `(gateway_id,message_id)`. El boot_id identifica reinicios. Ordenar datos actuales por tiempo fiable o uptime dentro del mismo boot; un backlog no marca un dispositivo como disponible. El worker usa una sesión MQTT persistente y confirma QoS1 después del commit PostgreSQL; mensajes inválidos se rechazan y se registran. La ubicación se captura en la recepción; datos sin fecha de origen fiable se rotulan como recepción y no se atribuyen retroactivamente.

Retención: 12 meses de muestras; resultados persistentes. Sesión opaca en cookie HttpOnly/SameSite, hash en DB; contraseñas scrypt. Autorización por rol en servidor y control Origin en mutaciones.

## Dependencia firmware

Añadir en H2/C6 comando Zigbee versionado, validación/persistencia NVS y ACK de aplicación. En C6 implementar Ethernet W5500, MQTT/TLS, identidad, reloj, capacidades y outbox de resultados. No reinterpretar el campo reserved de v1 sin versión de protocolo. Mantener compatibilidad con nodos antiguos y distinguir versión desconocida. El desarrollo web no equivale a validación física.

Los DXF originales y su geometría se guardan en PostgreSQL para que el respaldo sea transaccional con sus posiciones; no hay archivos DXF en una carpeta pública. El borrador es solo visible al administrador.

Gateways: `map_level_id` y coordenadas `x/y` describen su instalación física. La migración 002 asigna inicialmente el nivel si `location_id` ya es un nivel, pero no inventa coordenadas. Colocar el gateway sincroniza `location_id`; cambiar su ubicación desde administración borra coordenadas previas. Las lámparas conservan su nivel propio. `/api/events` añade el evento autenticado `gateways`, conservando el evento `snapshot` de lámparas. El estado incluye antigüedad por nodo y gateway; el cliente mantiene el vencimiento aunque se pierda SSE.
