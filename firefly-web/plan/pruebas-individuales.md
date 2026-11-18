# Pruebas individuales de 30 segundos

## Contrato MQTT

- Gateway publica `status` con `test_capable:true` solo con soporte y reloj civil válido. Ausencia del campo o gateway offline deshabilita la capacidad.
- Web publica QoS1, sin retain, a `firefly/v1/gateways/<gateway>/commands/test`:

```json
{"schema_version":1,"command_id":"UUID","device_id":"abcdef01","test_id":32768,"duration_s":30,"issued_at_ms":1790000000000,"expires_at_ms":1790000030000}
```

- Gateway responde en `/test-ack`: `schema_version`, `message_id`, `command_id`, `device_id`, `test_id`, `status` (`accepted`, `rejected`, `unknown`) y `reason` opcional. `accepted` confirma el envío hacia Zigbee, no el inicio ni la aprobación por la lámpara.
- Resultado en `/test-report` con el esquema existente, más `command_id` opcional. Se confirma únicamente si coinciden gateway, lámpara, UUID y test_id. Resultado original, incluida una falla, permanece intacto.

## Estados y recuperación

Pendiente → enviada → aceptada → resultado recibido; también rechazada, vencida, cancelada o incierta. UUID del navegador conserva idempotencia al repetir una petición HTTP. Worker marca envío antes de publicar y nunca lo reintenta por su cuenta. MQTT puede repetir paquetes QoS1; el C6 evita otra actuación usando registro NVS y marca temporal de máxima orden procesada. Una orden retain se descarta. Los fragmentos MQTT se ensamblan con límite de 512 bytes y desplazamientos comprobados.

C6 persiste antes de enviar; si reinicia entre el envío Zigbee y su confirmación persistente, queda incierta y no la ejecuta de nuevo. Repetir la consulta de esa orden informa incertidumbre. Una base restaurada o reloj del servidor atrasado puede producir rechazos de órdenes antiguas: no borrar NVS para forzar otra ejecución sin revisar la situación física.

Web no reutiliza test_id de un gateway; hay 32768 identificadores reservados por gateway. Agotarlos exige ampliar el contrato, no reciclar referencias históricas. Tras perder el resultado se espera tres minutos antes de permitir otra solicitud. Esta espera no demuestra que el relé esté en un estado concreto.

## Integración física pendiente

Se usa `SCHEDULE_TEST` Zigbee v1 con demora cero, duración 30 s e identificador reservado. No cambian structs ni constantes del protocolo común. El H2 actual persiste los programas y puede recuperarlos al reiniciar: el vencimiento MQTT limita la entrega al C6, pero no cancela una prueba ya transferida al H2. No se envían cancelaciones ni repeticiones ciegas ante esta ambigüedad. La validez del perfil de batería y del resultado eléctrico sigue siendo independiente del botón.

C6 bloquea la prueba remota durante campañas automáticas o ciclos repetitivos y reserva una ventana hasta recibir resultado o vencer. La ficha exige telemetría reciente, AC y sensor válido. El H2 mantiene sus comprobaciones y el límite local del relé. Debe validarse en banco con las placas reales; esta entrega compila C6 y prueba el transporte simulado, sin flashear ni actuar sobre dispositivos reales.
