# Configuración de batería pendiente en firmware H2/C6

Esta especificación coordina E01 y se limita al recorrido de configuración de batería, aún pendiente. El usuario confirmó funcionamiento de telemetría y prueba individual; eso no demuestra que H2 acepte perfiles de batería. La implementación corresponde al otro chat de firmware.

**Tarea ejecutable y seguimiento:** [FW-001 — Configuración de batería H2/C6](firmware/FW-001-configuracion-bateria-h2-c6.md). Esta página conserva la propuesta técnica previa; sus IDs y formatos deben verificarse antes de implementarlos.

## Responsabilidades

- H2: validar, persistir y aplicar el perfil; calcular alertas, intervalo adaptativo, evaluación de pruebas y estimación a partir del perfil activo. Conservar timeout local del relé. UVLO continúa siendo una bandera: esta configuración no autoriza cortar físicamente la carga.
- C6: Ethernet W5500 sin Wi-Fi, MQTT/TLS, credenciales propias, reloj sincronizado y transporte bidireccional Zigbee. Conservar consola USB y CSV existentes. No usar H2 coordinador/S3 como alternativa.
- Web: autoridad de versiones deseadas; mantener valor aplicado separado y no inferir aplicación de un PUBACK MQTT.

## Extensión Zigbee propuesta

Reservar `0x05` para configuración y `0x06` para su confirmación. Documentarlos en `firmware/PROTOCOL.md` y definirlos en `firmware/common/include/emergency_protocol.h` al implementar firmware. No reutilizar silenciosamente `reserved` de v1.

Solicitud v1: versión de formato `uint8_t`, UUID de orden `uint8_t[16]`, revisión por lámpara `uint32_t`, siete umbrales `uint16_t` en el orden del contrato MQTT. Valores little-endian y validación explícita de tamaño antes de copiar. UUID en bytes RFC4122; revisión monotónica, nunca usar el índice de versión del perfil como revisión del dispositivo.

Respuesta v1: versión, UUID, device_id, revisión, estado (`applied`, `deferred`, `rejected`) y motivo enumerado. El gateway publica el ACK correspondiente solo después de recibir esta respuesta del nodo.

Telemetría y reporte v2: incorporar revisión activa, capacidad de configuración y un identificador de arranque. Mantener decodificación v1 por versión y tamaño; v1 se publica con config_capable=false y sin config_version. La revisión no se deduce de la última orden transmitida.

## Persistencia y cambios durante prueba

Guardar un blob NVS versionado con validación de integridad. Aplicar el conjunto completo solo si NVS commit termina correctamente. Rechazar valores incoherentes o fuera de representación; rangos de la estimación se calculan por resta y siempre deben ser positivos.

Guardar también la revisión/UUID pendientes si la prueba está activa. Responder deferred y aplicar al finalizar. Una revisión superior sustituye a la pendiente inferior. Repetir la misma orden produce el mismo resultado sin escrituras innecesarias; una revisión inferior nunca restaura valores anteriores. Tras reiniciar, recuperar la configuración válida y comunicarla. Si no hay configuración, conservar explícitamente el perfil fijo heredado y anunciarlo como tal; no inventar un perfil de 4 V.

No cambiar umbrales a mitad de una prueba. El reporte debe incluir la revisión utilizada al iniciarla. Los resultados originales permanecen históricos.

## Verificación antes de cerrar E01–E04

1. Pruebas de codificación/decodificación, tamaños, valores inválidos, duplicados y orden de revisiones.
2. Compilar H2 y C6 con ESP-IDF 5.5; revisar los consumidores históricos si cambia la cabecera común, sin incorporarlos a la arquitectura final.
3. Confirmar NVS tras reinicio y corte durante escritura; rechazo no cambia el perfil previo.
4. Ensayo real con perfiles apropiados para cada batería, no con los perfiles sintéticos de demostración.
5. Verificar C6 → Zigbee → H2 → ACK → MQTT → web, pérdida de ACK, desconexión y cambio diferido durante una prueba.
6. Capturar evidencia y actualizar tareas. Una compilación exitosa tampoco demuestra funcionamiento en hardware.
