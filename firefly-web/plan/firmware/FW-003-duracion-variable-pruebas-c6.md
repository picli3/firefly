# FW-003 — Pruebas remotas de duración variable en el C6 (30–300 s)

- **Estado:** implementada y verificada en host y compilación (rama `feature/firmware_WG_test_duration`, commit `8760540`). **Pendiente:** cargar en el C6 y verificar en hardware.
- **Responsable:** chat de firmware C6.
- **Fecha:** 2026-11-18, America/Lima.
- **Alcance:** solo `firmware/c6-zigbee-gateway`. El H2 ya admite hasta `APP_MAX_TEST_DURATION_S = 300` s; no se modifica.
- **Origen:** planificación de pruebas por lámpara en la web (días, hora y duración), rama `feature/web_test_schedule`.

## Contexto

La web permite programar pruebas de batería por lámpara y elegir su duración: 30 s, 1, 2, 3, 4 o 5 min. A la hora programada el worker crea una solicitud de prueba y la publica por la misma ruta MQTT que el botón «Ejecutar prueba». Hasta ahora esa ruta solo admitía 30 s:

- `remote_test.c` descartaba cualquier `duration_s` distinto de 30.
- `zigbee_gateway.c` enviaba al H2 `SCHEDULE_TEST` con `duration_s = 30` fijo.
- La reserva de campaña y la ventana de «gateway ocupado» eran 180 s fijos.

## Contrato web → C6 (sin cambios de topic)

Topic `firefly/v1/gateways/<gateway>/commands/test`, QoS 1, sin retain:

```json
{
  "schema_version": 1,
  "command_id": "<UUID>",
  "device_id": "abcdef01",
  "test_id": 32768,
  "duration_s": 120,
  "issued_at_ms": 1763514000000,
  "expires_at_ms": 1763514030000
}
```

- `duration_s`: **entero entre 30 y 300**. Antes solo 30.
- `expires_at_ms - issued_at_ms ≤ 31000`, sin cambios. Es la ventana de envío, no la duración de la prueba.
- `test-ack` y `test-report`, sin cambios. El `test-report` lleva la `duration_s` real que informa el H2.

## Contrato C6 → web: capacidad

El mensaje `status` añade el campo **`test_max_duration_s`**, un entero de 30 a 300:

```json
{"schema_version":1,"message_id":"…","online":true,"boot_id":"…","test_capable":true,"test_max_duration_s":300}
```

- La web guarda el valor por gateway.
- Si falta, por ejemplo con firmware anterior, la web asume 30 s.
- La web no ofrece duraciones mayores al máximo anunciado y rechaza guardar una planificación que lo supere.

> **Orden de despliegue obligatorio:** primero la web con este cambio y después el C6. La web anterior valida `status` de forma estricta: si recibe un campo desconocido, rechaza el mensaje y el gateway figuraría como desconectado.

## Cambios requeridos en el C6

1. **Validación.** `remote_test.c` debe aceptar `duration_s` entero (`duration == floor(duration)`) en `[APP_REMOTE_TEST_MIN_S, APP_REMOTE_TEST_MAX_S] = [30, 300]`. Un valor fuera de rango o con decimales se descarta sin `test-ack`, igual que cualquier orden inválida.
2. **Envío al H2.** `zigbee_gateway_start_remote_test(device, test_id, duration_s)` debe enviar `SCHEDULE_TEST` con demora 0 y esa duración. Debe rechazar con `ESP_ERR_INVALID_ARG` un valor fuera de rango.
3. **Ventanas.** La reserva de campaña automática y la ventana en que el gateway rechaza otra prueba remota pendiente deben durar `duration_s + APP_REMOTE_TEST_MARGIN_S (150 s)`. Con 30 s equivale a los 180 s actuales.
4. **Persistencia sin cambiar NVS.** La duración pedida se guarda en `s_record.report.duration_s`. Ese informe está vacío hasta que llega el resultado, así que el blob `remote_test/last` conserva su tamaño y `RECORD_VERSION = 2`. Un registro escrito por firmware anterior tiene 0 en ese campo y se trata como 30 s. Al reiniciar con una prueba pendiente (estado 1/2), la reserva usa esa duración.
5. **Capacidad.** `publish_status()` debe incluir `"test_max_duration_s": APP_REMOTE_TEST_MAX_S`.
6. **Constantes** en `main/app_config.h`: `APP_REMOTE_TEST_MIN_S 30`, `APP_REMOTE_TEST_MAX_S 300` y `APP_REMOTE_TEST_MARGIN_S 150`. `APP_REMOTE_TEST_MAX_S` no debe superar el límite del H2.
7. **Documentación:** actualizar `firmware/PROTOCOL.md`.

No cambian las estructuras de `firmware/common`, los IDs de prueba (32768–65535 remotas, 1–32767 campañas), la deduplicación por UUID/`highwater` ni el comportamiento fail-closed tras reinicio.

## Criterios de aceptación

- [x] Orden con `duration_s` 30, 60 y 300: `test-ack accepted` y `SCHEDULE_TEST` al H2 con esa duración.
- [x] Orden con 29, 301, 45.5, 0 o −30: descartada, sin envío Zigbee ni `test-ack`.
- [x] Con una prueba de 300 s pendiente, otra orden se rechaza («Otra prueba remota pendiente») hasta 450 s después de emitida. Pasado ese plazo se acepta.
- [x] Registro pendiente de firmware anterior (duración 0): se mantiene la ventana de 180 s.
- [x] Deduplicación, vencimiento, actualización v1 y reenvío de resultado tras reinicio siguen funcionando (prueba de host existente).
- [x] `idf.py build` sin errores ni avisos.
- [ ] C6 cargado. `status` en la web muestra «hasta 5 min» y la planificación permite elegir más de 30 s.
- [ ] Prueba física autorizada de 60 s o más: el relé del H2 abre durante la duración pedida, el `test-report` informa esa `duration_s` y la web registra el resultado en «Ejecuciones programadas».

## Devolución

**Archivos modificados** (rama `feature/firmware_WG_test_duration`):
- `main/app_config.h`: constantes de rango y margen.
- `main/remote_test.c`: validación 30–300, duración persistida en el registro, ventana de ocupado y reserva tras reinicio.
- `main/zigbee_gateway.c` y `.h`: nueva firma con `duration_s`, reserva `duration + 150` y `SCHEDULE_TEST` con la duración pedida.
- `main/ethernet_mqtt.c`: `status` con `test_max_duration_s`.
- `tools/tests/remote_test_host.c`: casos de rango válido e inválido, ventana de ocupado de 300 + 150 s y compatibilidad con registros anteriores.
- `firmware/PROTOCOL.md`.

**Verificado:**
- `python3 tools/tests/test_remote_test.py`: OK. Ejercita el parser y la persistencia reales con *stubs* de host.
- `test_remote_config.py` y `test_at_provisioning.py`: OK.
- `idf.py build` (ESP-IDF 5.5): imagen de 0x10b5c0 bytes, 24 % libre.
- Ruta web completa con un gateway simulado por MQTT en la instalación local: planificación a una hora concreta, orden publicada con `duration_s: 120`, confirmación, resultado y ejecución registrada.

**Pendiente:**
- Cargar el C6 (`idf.py -p <puerto> flash`). No borra NVS ni la red Zigbee: no usar `erase-flash`.
- Verificación física autorizada.
