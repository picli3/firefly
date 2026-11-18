# Verificación local — 2026-11-17

## Resultado

La webapp compila y funciona en Docker Compose con Nuxt, PostgreSQL, Mosquitto, worker y Caddy/HTTPS. La web publica HTTPS en `0.0.0.0:8443` para acceso LAN; PostgreSQL y MQTT sin TLS no publican puertos en el host. **No se validó ni modificó el firmware H2/C6.**

- `npm run check`: control de tipos correcto.
- `npm run build`: compilación de producción correcta.
- `docker compose build web` y arranque de todos los servicios: correctos.
- `npm audit`: cero vulnerabilidades reportadas al cerrar la implementación.
- `npm test`: diecisiete pruebas unitarias pasan; integración y MQTT se omiten por defecto y se ejecutan explícitamente.
- Integración sobre PostgreSQL real y aplicación Compose por HTTPS con CA confiada: siete escenarios pasan (ocho resultados contando el contenedor de la suite). Privacidad, Origin, roles, jerarquía, descubrimiento, validación de perfil, ACK diferido/aplicado, ACK antiguo ignorado, deduplicación, historial, DXF, posiciones, PDF/CSV y revocación de SSE.
- Mosquitto TLS real: contraseña incorrecta rechazada; publicación ajena y envío de órdenes por gateway rechazados; publicación propia permitida.
- Recuperación del worker: detenido durante cinco publicaciones QoS1; al reiniciar recibió 5/5. Los mensajes `live:false` no marcaron conexión actual.
- Configuración de demostración: recorrido real web → Mosquitto → simulador → ACK; datos y perfiles inequívocamente sintéticos. Ningún resultado acredita funcionamiento en una lámpara física. Se confirmaron los dos perfiles de 4 V/6 V y sus revisiones se conservaron después de reiniciar el simulador Compose. La prueba detectó y permitió corregir una carrera de arranque al restaurar su estado antes de registrar los listeners MQTT.
- Respaldo: checksums correctos; restauración PostgreSQL en base aislada con usuarios, planos y muestras; archivos de autenticación/certificados extraídos a carpeta aislada. Base activa intacta.
- Navegador: login, panel con telemetría, administración, visor DXF y marcadores, ficha con configuración aplicada y consulta de reportes. Revisión a 390 px sin desbordamiento global horizontal; tablas y navegación admiten desplazamiento interno.
- PDF: renderizado con Poppler y revisión visual. Fuentes DejaVu embebidas con licencia incluida para evitar sustituciones y caracteres defectuosos.

## Repetir pruebas en desarrollo

No ejecutar fixtures o scripts de demostración contra producción. Para desarrollo temporal, cree un archivo local ignorado `.runtime/test-ports.yaml`:

```yaml
services:
  db:
    ports: ["127.0.0.1:55432:5432"]
  mosquitto:
    ports: ["127.0.0.1:51883:1883"]
```

```sh
docker compose -f compose.yaml -f .runtime/test-ports.yaml up -d
npm ci
npm run dev
# En otra terminal, con Node >=22.21:
INTEGRATION=1 node scripts/local.mjs --test tests/integration.test.mjs
```

Para verificar Compose por HTTPS sin omitir validación TLS:

```sh
docker compose cp proxy:/data/caddy/pki/authorities/local/root.crt .runtime/caddy-root.crt
INTEGRATION=1 TEST_HTTPS=1 NODE_EXTRA_CA_CERTS=.runtime/caddy-root.crt node scripts/local.mjs --test tests/integration.test.mjs
```

Para probar broker y simulador:

```sh
node scripts/local.mjs scripts/demo.mjs
docker compose cp mosquitto:/mosquitto/certs/server.crt .runtime/mqtt-ca.crt
MQTT_TEST=1 node --test tests/mqtt.test.mjs
node scripts/local.mjs scripts/run-simulator.mjs
```

`recovery-check.mjs` detiene/reanuda el worker de este Compose y comprueba la cola persistente; no toca hardware. `verify-demo-config.mjs` requiere el simulador Compose activo, aplica perfiles sintéticos y lo reinicia para comprobar su volumen. Las credenciales de demo quedan únicamente en archivos ignorados con permisos restringidos.

Al terminar, detener simuladores, revocar cuentas temporales de QA y cerrar el servidor de desarrollo. El Compose principal conserva sus volúmenes. Para quitar los puertos de desarrollo, ejecutar `docker compose up -d` sin el archivo adicional.

## Pendiente

E01–E04 requieren implementar el protocolo/firmware y validar el recorrido en placas reales. E05 tiene verificación de servicios, pero su parte física continúa pendiente. Las especificaciones están en [firmware.md](firmware.md). Los resultados actuales no autorizan afirmar que el C6 publique MQTT ni que el H2 aplique umbrales remotos.

## Ampliación: registro de gateway

Alta y renovación entregan un paquete privado con datos MQTT y certificado público. Integración por HTTPS contra Mosquitto: credenciales aceptadas, rotación, verificación repetida, prohibición para monitor y recuperación posterior sin contraseña. AT es un contrato provisional sin firmware C6; se verificaron reconstrucción de JSON, fragmentos e integridad, no ejecución en hardware. Nuevos avisos distinguen autenticación, configuración de red y soporte del firmware.

Navegador: alta real de un gateway de QA, modal automático, contraseña oculta, descarga JSON validada, bloque AT provisional visible y consulta posterior sin secreto. Captura local en `.runtime/screenshots/gateway-registration.jpg`. La cuenta y el gateway temporales se revocaron/eliminaron tras verificar; no se modificaron los registros del usuario.

## Plantilla mensual y SVG Firefly

Diseño reproducido a partir del PDF proporcionado. Trece pruebas unitarias y seis escenarios de integración HTTPS pasan, incluyendo generación del nuevo PDF, privacidad y rechazo de rangos de más de 12 años. Se comprobaron promedios por unidad, datos ausentes, sensor inválido, fechas de Lima y PASS original. Renderizados: ejemplo de cinco páginas, vacío de dos y estrés de 14 páginas. El SVG es idéntico en origen y activos; el PDF lo conserva vectorial.

Logo comprobado visualmente en el login, servido desde `/branding/firefly.svg`. La copia coincide con el original proporcionado; también se incluyó en navegación y favicon. No se modificaron firmware, planos ni datos operativos para esta ampliación.

## Gateways y actividad en planos

Migración 002 aplicada en PostgreSQL, compilación de producción Docker y comprobación TypeScript. Diecisiete pruebas unitarias pasan; siete escenarios de integración HTTPS pasan (ocho resultados TAP contando el contenedor). Cubren colocación persistente, rechazo de ubicación fuera de nivel, coordenadas incompletas, monitor sin permiso, revisión explícita de gateways al reemplazar plano, nodo silencioso con gateway vigente, alerta independiente y caída del gateway.

Navegador con datos aislados y MQTT real a Mosquitto: gateway azul, lámpara A verde tras su mensaje mientras B sigue gris; A vence sin recarga y luego B pasa a verde con su mensaje manteniendo A gris. Una alerta no reemplaza el verde de comunicación. Colocación desde el visor confirmada y coordenadas comprobadas en PostgreSQL. Captura `.runtime/screenshots/map-gateway-activity.jpg`. La cuenta temporal fue revocada y los registros sintéticos se eliminaron; registros operativos preservados. No se realizó validación física ni cambios de firmware.

## Acceso LAN solicitado

`.env` actualizado con `WEB_BIND=0.0.0.0`, `SITE_ADDRESS=192.168.18.86` y `PUBLIC_ORIGIN=https://192.168.18.86:8443`. Se validó y recargó Caddy con `default_sni` para conexiones TLS por IP detrás de Docker. Servicios levantados sin las exposiciones temporales de QA. Login por IP devuelve 200, API anónima 401; login real, cookie Secure, consulta autenticada de lámparas/gateways/planos y logout correctos, verificando TLS contra la CA local. No se ha probado desde un segundo dispositivo físico. La CA pública está en `firefly-local-ca.crt` para instalar en los clientes.

## Papelera de inventario

Pruebas sobre un proyecto Compose independiente (`firefly-trash-test`), con PostgreSQL, broker, credenciales y volúmenes propios: siete escenarios previos de integración y un escenario nuevo de papelera pasan (nueve resultados TAP contando el contenedor de los escenarios previos). El caso nuevo cubre privacidad, monitor sin permisos, bloqueo de dependencias, retirada/restauración de sede/edificio/nivel/gateway/lámpara, planos y DXF privados, ausencia de resurrección por telemetría, mediciones históricas conservadas y CSV recuperable. Mosquitto real rechaza nuevas conexiones después de retirar un gateway y las admite al restaurarlo. Diecisiete pruebas unitarias y control TypeScript pasan; compilación Docker correcta.

Navegador en instalación aislada: confirmación de envío, registro visible en Papelera, restauración y reaparición en Ubicaciones. Captura `.runtime/screenshots/inventory-trash.jpg`, con datos sintéticos. No se retiraron registros reales. Respaldo local anterior a la migración en `.runtime/before-inventory-trash.sql` (privado, ignorado por Git). Migración 003 añade `deleted_at` y guardas de dependencia a nivel de PostgreSQL. La web y el worker se actualizan manteniendo broker y base de datos activos.

## Prueba individual desde la ficha

Diecisiete pruebas unitarias, control TypeScript y compilación Docker pasan. En Compose aislado, los siete escenarios previos más papelera y prueba individual pasan (diez resultados TAP con el contenedor de la suite previa). El caso nuevo recorre API → MQTT real → gateway simulado → confirmación → resultado y verifica monitor sin permisos, firmware sin capacidad, AC ausente, UUID repetido, exclusión por gateway, reporte de nodo/UUID incorrecto, falta de resultado, confirmación tardía, orden vencida y ausencia de reenvío automático. No se actuó sobre las lámparas reales.

C6 compila con ESP-IDF 5.5, Ethernet/TLS habilitados y módulo remoto incluido. Dieciocho pruebas de herramientas firmware pasan, incluida la prueba de host del parser C real con cJSON y puertos simulados: persistencia previa al envío, duplicados, reinicio entre actuación y commit, NVS no disponible, expiración y fragmentación MQTT. No se modifican structs comunes ni firmware H2; no se flasheó la placa. J04 continúa pendiente de validación física.

Navegador aislado: botón, confirmación, estado pendiente, bloqueo mientras espera y resultado PASS simulado visibles. Captura `.runtime/screenshots/individual-test.jpg`. La simulación se detuvo y sus contenedores/volúmenes de prueba se retiraron. Instalación LAN actualizada con migración 004, web/worker nuevos y recarga SIGHUP de ACL; broker y base permanecieron activos. Login, cookie Secure y consultas autenticadas correctos. Se confirmaron cero solicitudes físicas creadas por este despliegue y recepción de mediciones reales después de actualizar. Copia previa de base en `.runtime/before-individual-tests.sql`, privada e ignorada por Git.

## Corrección de entrega de órdenes al C6 — 2026-11-17

Diagnóstico sobre la instalación real: gateway anunció capacidad, dos solicitudes llegaron a estado enviado pero no había `test-ack`. El broker leía `/mosquitto/auth/acl`, cuya copia se había creado solo al arrancar; faltaban los permisos `commands/test` y `test-ack` añadidos a `docker/acl`. Recargar Mosquitto sin copiar la política no actualizaba esas reglas.

Se actualizó la copia privada y se recargó con SIGHUP, sin reiniciar el broker. Una orden de diagnóstico con fechas Unix 1–2 ms (siempre vencida para un reloj admitido por el firmware) recibió del C6 real `rejected: Orden vencida, antigua o reloj no sincronizado`. No se solicitó actuación Zigbee ni se reintentaron órdenes anteriores. Esto confirma recepción MQTT y respuesta del C6; no acredita todavía la prueba eléctrica en H2.

Corrección permanente: worker monta la fuente ACL de solo lectura, detecta cambios independientemente de las contraseñas, escribe copia atómica con propietario 1883 y modo 640, y recarga Mosquitto. Nueva prueba de regresión en Compose aislado reproduce política antigua, cambia solo ACL, comprueba envío y recepción con las mismas credenciales y mantiene denegado publicar en otro gateway. El broker del ensayo no se reinicia. No se modificó ni cargó firmware para esta corrección.
