# FW-001 — Configuración de batería del H2 a través del C6

- **Estado:** implementada, compilada y validada en lámpara de 4 V; aceptación total pendiente de segunda lámpara y ensayo de corte durante escritura (2026-11-17).
- **Prioridad:** alta; bloquea aplicar perfiles reales desde la web.
- **Responsable:** chat de firmware ESP32-H2/ESP32-C6.
- **Solicitante:** chat de FireFly Web.
- **Fecha:** 2026-11-17, America/Lima.
- **Relacionada con:** E01–E03 y C01–C07 de `firefly-web/plan/tareas.md`.

## Problema y resultado esperado

El usuario confirma que las lámparas H2 actuales no permiten cargar su configuración de batería. La web ya permite crear/versionar perfiles y asignarlos por lámpara, pero debe seguir mostrando falta de soporte mientras el firmware anuncie `config_capable:false`.

Implementar el recorrido **web → MQTT → C6 → Zigbee → H2 → persistencia/aplicación → ACK → C6 → MQTT → web**. Cada H2 debe usar sus propios umbrales para sus alertas y evaluación de pruebas, de acuerdo con la batería instalada. La recepción por el C6 o un PUBACK MQTT no prueban aplicación en la lámpara.

La telemetría real y la prueba individual web ya funcionan, según confirmación del usuario. No sustituir ni romper esos recorridos. El trabajo se coordina por este archivo: el chat web no editará firmware.

## Contrato MQTT que la web implementa hoy

Raíz: `firefly/v1/gateways/<gateway_id>`.

### Solicitud: `/commands/config`

QoS 1, sin retain. Campos:

| Campo | Tipo y significado |
| --- | --- |
| `schema_version` | Entero `1` |
| `command_id` | UUID de orden; estable en reintentos |
| `device_id` | Identificador de la lámpara, igual al publicado en telemetría |
| `version` | Entero positivo, revisión deseada **por lámpara**; no es la versión del perfil de catálogo |
| `thresholds` | Objeto completo con los siete campos siguientes |

Todos los umbrales son enteros en **milivoltios**, positivos y hasta 65535:

| Campo | Uso |
| --- | --- |
| `attention_mv` | Umbral de atención |
| `alert_mv` | Umbral de alerta |
| `critical_mv` | Umbral crítico |
| `test_min_mv` | Límite mínimo para evaluar la prueba |
| `full_mv` | Referencia de batería llena |
| `ac_empty_mv` | Referencia de vacío con AC |
| `dc_empty_mv` | Referencia de vacío sin AC |

Validaciones actuales de la web:

```text
critical_mv <= alert_mv < attention_mv <= full_mv
ac_empty_mv < full_mv
dc_empty_mv < full_mv
test_min_mv <= full_mv
```

La química y tensión nominal se guardan en el perfil web y se valida compatibilidad nominal al solicitar el cambio. **El mensaje MQTT actual no incluye química, tensión nominal, SoH calculado ni rangos derivados.** No exigir esos campos sin proponer antes una ampliación del contrato. Tampoco asumir que dos baterías de igual voltaje tienen los mismos límites: los valores reales deben validarse para la batería instalada, sin usar perfiles sintéticos como datos de producción.

La web reintenta configuraciones con el mismo `command_id`, `device_id` y `version`. Estas son configuraciones idempotentes; no confundir con las órdenes de prueba física, que no se reenvían automáticamente.

### Respuesta: `/config-ack`

Campos obligatorios: `schema_version:1`, `message_id` único, `command_id`, `device_id`, `version`, `status`. Campo opcional `reason` de hasta 500 caracteres.

- `applied`: H2 persistió y aplicó el conjunto íntegro.
- `deferred`: H2 está en prueba y guardó el cambio pendiente; todavía no lo aplicó.
- `rejected`: H2 rechazó la orden, con motivo.

El C6 debe publicar la respuesta real del H2, sin inventar `applied` tras recibir o transmitir una orden. UUID, revisión y nodo deben coincidir. La web ignora ACK de órdenes sustituidas o revisiones que ya no son la deseada.

### Telemetría y resultados

- Telemetría mantiene los campos existentes y anuncia `config_capable:true` solo si **ese H2** admite este recorrido completo mediante el C6.
- `config_version` opcional debe informar la revisión realmente activa en H2, no la última solicitada por la web o recibida por el gateway.
- El reporte de prueba debe conservar la revisión usada al iniciar la prueba, aunque luego se aplique otro perfil.
- Nodos anteriores siguen con `config_capable:false` y revisión desconocida; no forzar capacidad desde el C6 o la base de datos.
- Sin configuración válida, declarar el perfil heredado/no confirmado; no afirmar que se aplicó una batería distinta.

## Trabajo requerido en H2

- [x] Definir decodificación y validación de un comando Zigbee versionado para el conjunto completo.
- [x] Persistir configuración, revisión y UUID en NVS con formato versionado e integridad verificable.
- [x] Aplicar atómicamente solo después de un commit correcto; conservar el perfil previo ante error.
- [x] Usar los valores activos en alertas, estimación y evaluación de prueba, revisando los cálculos y evitando divisiones por cero.
- [x] Si hay prueba en curso, persistir el cambio pendiente, responder `deferred` y aplicar al terminar, conservando la configuración original del reporte.
- [x] Hacer idempotentes los duplicados; rechazar regresiones de revisión y contenido diferente para una misma identidad/revisión.
- [x] Recuperar configuración activa/pendiente al reiniciar; volver a informar capacidad, revisión real y ACK necesario.
- [x] Mantener las protecciones locales del relé y el comportamiento eléctrico existente. UVLO no autoriza nuevos cortes físicos de carga.

## Trabajo requerido en C6

- [x] Recibir `/commands/config` además de `/commands/test`, ensamblando/validando correctamente mensajes MQTT fragmentados.
- [x] Resolver `device_id` al H2 correcto y transportar el conjunto, UUID y revisión por Zigbee.
- [x] Recibir la confirmación del H2 y publicar `/config-ack` con sus datos originales.
- [x] Propagar capacidad y revisión por nodo, permitiendo convivencia de H2 anteriores y nuevos.
- [x] Propagar la revisión correcta en cada medición/reporte y conciliar al reconectar.
- [x] Conservar telemetría, CSV/USB, TLS, credenciales, pruebas individuales y deduplicación ya existentes.

La ACL web ya permite al gateway leer su raíz `/commands/config` y escribir `/config-ack`. Su sincronización con Mosquitto está corregida. Verificar permisos efectivos al ensayar, sin atribuir un rechazo del broker a Zigbee.

## Protocolo y fuentes que revisar

La propuesta previa está en `firefly-web/plan/firmware.md`: IDs Zigbee `0x05`/`0x06`, UUID en bytes RFC4122, enteros little-endian, nueva versión de telemetría/reporte. **Son propuestas, no una asignación confirmada:** comprobar primero colisiones y el estado actual del firmware. No reutilizar silenciosamente `reserved` ni cambiar la interpretación de tramas v1.

Fuentes web del contrato (relativas a `firefly-web/`):

- `services/lib/domain.mjs`: esquemas y validación de umbrales, telemetría, reporte y ACK.
- `services/lib/ingest.mjs`: entrega/reintento de configuración y tratamiento del ACK.
- `services/lib/api.mjs`: selección de perfil, compatibilidad nominal y revisión por lámpara.
- `app/pages/lamparas/[id].vue`: configuración solicitada/aplicada y bloqueo de firmware incompatible.
- `services/simulator.mjs`: ejemplo simulado; no demuestra compatibilidad física.

En firmware, seguir los `AGENTS.md` aplicables, el protocolo actual y las fuentes reales. ESP-IDF 5.5. Si se cambia la cabecera común, compilar H2/C6 y revisar los consumidores históricos H2-coordinator/S3 sin cambiar la arquitectura principal.

## Criterios de aceptación

- [x] Compilan los proyectos afectados y se registran versiones/variantes de firmware ensayadas.
- [ ] Dos H2 con perfiles distintos usan y conservan sus propios valores.
- [x] Un perfil inválido se rechaza sin modificar el anterior (host; conflicto de contenido también comprobado en hardware).
- [x] Repetir una orden no vuelve a escribir innecesariamente NVS ni cambia su resultado; una revisión vieja no restaura valores anteriores. Host comprueba escrituras; hardware confirma ACK y rechazo de revisión vieja.
- [ ] Tras reinicio/corte durante escritura, queda una configuración íntegra y la revisión publicada es la real.
- [x] Una configuración solicitada durante prueba queda diferida y el reporte conserva la revisión usada.
- [x] El ACK confirmado del H2 llega a la web y esta muestra el valor aplicado; no solo el solicitado.
- [x] Nodos sin soporte continúan identificándose como incompatibles.
- [x] Telemetría y prueba individual existentes siguen funcionando.

## Devolución del chat de firmware

Actualizar esta sección al trabajar en la tarea, distinguiendo pruebas de software y de hardware. No marcar completada solo por compilar.

- Estado: implementación, compilaciones y ensayo completo con una lámpara terminados;
  no se marca aceptación total por los pendientes físicos indicados abajo.
- Archivos: `firmware/common/include/emergency_protocol.h`, `firmware/PROTOCOL.md`;
  H2: nuevos `main/battery_config.c/.h`, integración en `main.c`, `battery_test.c/.h`,
  `telemetry.c`, `zigbee_node.c/.h`, CMake y README;
  C6: nuevos `main/remote_config.c/.h`, integración en `ethernet_mqtt.c/.h`,
  `zigbee_gateway.c/.h`, `node_registry.c/.h`, `remote_test.c/.h`, `csv_serial.c`, CMake,
  README y texto de `tools/configure_mqtt.py`; pruebas host en ambos proyectos.
- Contrato MQTT: compatible con el esquema actual, sin campos nuevos obligatorios.
  Solo ACK real del H2; config/version por nodo, `config_version` omitida cuando
  es cero/heredada. No se agrega `config_capable` al resultado de prueba porque
  `testSchema` no lo admite. UUID transportado en octetos RFC4122, MQTT canónico
  en minúsculas. Se valida versión positiva hasta uint32 y siete mV uint16.
- Zigbee: `0x05` solicitud v1/35 bytes, `0x06` ACK v1/27 bytes, sin colisiones.
  Telemetría v2/33 bytes y reporte v2/35 bytes anexan revisión, capacidad y boot ID;
  v1/24 y 26 bytes siguen admitidos sin reutilizar `reserved`.
- Persistencia H2: blob NVS versionado con CRC32, activo/pendiente y UUID.
  Aplicación tras commit; error conserva estado previo. Duplicado exacto no
  escribe. Reinicio recupera pendiente; ACK activo/pendiente se reanuncia cada
  30 s. Corrupción se preserva y rechaza configuración (`storage`), sin formatear.
- Reconexión C6: caché RAM FIFO de 16 respuestas reales, mismo `message_id` en
  reenvío. No es durable; tras reinicio recupera por reintento web al H2 o su
  reanuncio. No se generan ACK de aplicación por recepción MQTT/envío Zigbee.
- CSV mantiene formato y datos; no incorpora revisión. MQTT sí conserva revisión
  en cada muestra y en el reporte original, incluso al reproducir resultado guardado.
- Compilaciones con ESP-IDF 5.5.0: H2 router ZCZR, C6 W5500+MQTT/TLS y C6 USB
  sin Ethernet en build aislado `/tmp/fw001-c6-usb`: todas correctas.
  H2: 632848 bytes; C6 TLS: 1088016 bytes; C6 USB: 617104 bytes.
  Los consumidores históricos `h2-coordinator` y `s3-gateway` no existen en este
  checkout; se conservó el contrato v1 documentado, sin afirmar compilación.
- Software: 3 pruebas host H2 ejecutan C real (estado/NVS, telemetría, pruebas).
  Cubren dos imágenes de flash independientes, umbrales sintéticos 4/6/12 V,
  valores inválidos, duplicados sin escritura, revisión vieja/conflicto,
  fallos set/commit, reinicio y CRC, diferido y revisión de inicio del reporte,
  temperaturas negativas y SoH limitado. No son perfiles recomendados de batería.
  C6: 20 pruebas correctas incluyendo CSV/descarga, ciclos y prueba individual,
  actualización de deduplicación v1, revisión guardada del resultado y configuración.
  Parser de configuración con ASan/UBSan y ACK contrastados con Zod real.
  Además, **107 mensajes MQTT reales** capturados durante el ensayo pasaron los
  esquemas actuales de telemetría, estado, configuración y reportes de la web.
- Carga física: C6 USB `/dev/ttyACM0`, W5500/TLS, y H2 lámpara de 4 V
  `/dev/ttyACM1`, ID `f96afdfc`, flasheados sin borrar NVS/Zigbee/CSV.
  Antes se cargó también un DevKit H2 de banco que el usuario retiró.
- Evidencia inicial: MQTT real de `gateway-standar` anuncia
  `f96afdfc`, `config_capable:true`, boot `h2-f96afdfc-f37350d1`, revisión
  ausente (heredada). C6 restauró PAN `0x584a`, canal 15, obtuvo IP y conectó
  MQTT tras NTP. `csv status`: mounted=1 recording=1 full=0 bytes=288447,
  boot=135, queue_drops=0 write_errors=0. No se borraron datos.
- Perfil de ensayo: al consultar la web, `f96afdfc` tenía desired_version=0,
  sin orden/perfil asignado. Solo existían perfiles `SIMULACIÓN 4 V` y
  `SIMULACIÓN 6 V`, explícitamente no aptos para hardware. Se pidieron al usuario
  química/modelo o umbrales validados; no se aplicaron esos perfiles sintéticos.
  El usuario identificó la **OPALUX DH-425D 4 V 2,5 Ah** y autorizó expresamente
  un perfil provisional para ensayo supervisado. Se creó por API autenticada,
  sin editar servicios ni forzar capacidades/estados en PostgreSQL:
  **ENSAYO NO VALIDADO — OPALUX DH-425D 4V 2.5Ah**.
  Umbrales mV: atención 4100, alerta 4000, crítico 3800, test_min 3800,
  llena 4600, vacío AC 4000 y vacío sin AC 3800.
  Son criterios provisionales de monitorización, **no límites certificados de
  OPALUX ni un perfil validado de producción**. La ficha de la marca confirma
  plomo-ácido, tensión y capacidad, pero no esos siete límites:
  [DH-425D, OPALUX/Life](https://opalux.lifestore.pe/producto/bateria-4v-2-5ah-opalux-nueva-100-original-tipo-seca-de-acido-de-plomo-para-luces-de-emergencia-balanzas-y-otros/).

### Evidencia física final, 2026-11-17 (horas UTC)

| Comprobación | Resultado observado |
| --- | --- |
| Web → worker MQTT → C6 → H2 → ACK → web | Orden `b1a27b2e-5980-4dbe-b7c5-2242cbad88e1`, revisión 1: `applied` a las 22:24:59.690; web desired/applied 1/1; muestra 4501 mV, flags 9, SoH estimado 83, config_version 1 |
| Revisión 2 y reinicio H2 | Orden `9ba1f9a5-145f-4e6f-aefb-ca03dd85887b`, mismo perfil. Tras reiniciar, boot `h2-f96afdfc-52ff2db6`, uptime 0, revisión 2 y ACK real `applied` |
| Duplicado exacto | Reenvío MQTT de revisión 2/UUID/contenido iguales → `applied` con mismo message_id de respuesta; ausencia de escritura NVS redundante comprobada en host |
| Revisión antigua | Reenvío revisión 1 tras activa 2 → `rejected`, reason `stale`; no volvió a perfil anterior |
| Conflicto | Mismo UUID/revisión 2 cambiando attention a 4150 → `rejected`, reason `conflict`; activa se conservó |
| Diferido físico | Prueba web `c3272486-43a4-4e1d-8e53-6aeb74247587`, test_id 32780, 30 s, comenzó con revisión 2. Orden `50c6ded8-7a7c-4d3b-918d-abd6adf4b285`, revisión 3 del mismo perfil aprobado, respondió `deferred` a las 22:26:20.750; web desired/applied 3/2 y telemetría conservó revisión 2 durante prueba |
| Fin y correlación | Reporte 22:26:37.935 conservó config_version 2. Después ACK revisión 3 `applied` 22:26:38.960, telemetría revisión 3/flags 9; web desired/applied 3/3, prueba completed, AC=true/testing=false |
| Resultado eléctrico | `LOW_CURRENT`, 7 muestras, V_before 4495 mV, V_min 4486 mV, I_avg 0 mA, I_peak 11 mA, P_avg 10 mW, T_max 30,73 °C. El pico no llegó al mínimo fijo de 50 mA; esto **no confirma iluminación/descarga normal ni capacidad de batería** |
| Reinicio C6 y H2 | C6 reenvió resultado persistido con config_version 2 y boot H2 original, live=false, sin ejecutar otra prueba. H2 volvió con boot `h2-f96afdfc-0bd7e528`, uptime 0, config_version 3 y ACK `applied`; web confirmó 3/3 |
| Coexistencia | `8e6dcd6a` siguió llegando como v1 con config_capable=false y sin revisión. No se actualizó ni configuró esa lámpara |
| USB/CSV | Descarga final `/tmp/fw001-telemetry-final.csv`: **291672 bytes, 4946 filas, 12 columnas, CRC32 9c5a3a21**, solo encabezado/filas. Persistencia de registros previos, sin borrado |

Durante la primera descarga de ~291 KiB se detectó watchdog de IDLE:
`taskYIELD()` no dejaba ejecutar tareas de menor prioridad; el diagnóstico del
watchdog eludía el filtro de logs y se intercalaba en USB. El descargador rechazó
el CRC y conservó `.part`; no entregó un CSV corrupto como válido. Se cambió por
`vTaskDelay(1)` entre bloques en `csv_serial.c`, se recompiló/flasheó C6 y se
repitió correctamente la descarga anterior, manteniendo MQTT y registro activos.

Binarios finales cargados (SHA256):

- H2 router: `921b8fe2f773d73159cf8ba1b79943868bf22a41ceca1461dd5c384205f822ce`.
- C6 W5500/TLS: `51a2e5310eaa09919cacd5fd1da244ad3905b0ccd5a6b4370b3d494e01c41f0d`.

### Pendientes y devolución para la web

- Actualizar/ensayar la segunda lámpara para confirmar dos perfiles independientes
  en hardware; en software se verificaron dos imágenes NVS separadas.
- Corte de energía **durante** escritura no ensayado físicamente; fallos set/commit,
  corrupción y recuperación sí se probaron en host, y reinicios completos en hardware.
- Perfil definitivo de batería pendiente de validación; queda aplicado el perfil
  de ensayo que autorizó el usuario, revisión 3. No hay ciclos automáticos nuevos.
- Contrato MQTT web sin cambios necesarios. Se usaron endpoints reales de perfil,
  configuración y prueba; los estados de aplicación proceden exclusivamente del ACK.
- Hallazgo web: `ingest.mjs` usa `uptime_s <= latest_uptime` como mensaje viejo.
  Dos muestras legítimas del mismo segundo (final de prueba y aplicación) tienen
  distinta secuencia/revisión; la segunda puede quedar fuera de `latest` hasta
  la siguiente muestra, aunque el ACK ya actualizó `applied_version`. Se observó
  temporalmente latest.config_version=2 con applied_version=3. Revisar la
  comparación usando también secuencia dentro del mismo boot, sin inventar uptime.
  No se modificó ese código desde firmware; después del reinicio, latest=3/active=3.
