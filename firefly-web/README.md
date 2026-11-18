# FireFly Web

Aplicación privada Nuxt para supervisar luminarias, ubicarlas en planos DXF y descargar reportes. Incluye perfiles versionados de batería con entrega MQTT y confirmación del nodo.

**Estado:** implementación local verificable con PostgreSQL, Mosquitto y simulador. La integración real con C6/H2 sigue pendiente; no se ha modificado ni flasheado el firmware. [Tareas](plan/tareas.md) y [dependencia firmware](plan/firmware.md).

## Arranque con Docker Compose

Requisitos: Docker con Compose. Los contenedores usan Node 22; no hace falta cambiar el Node del equipo.

1. Copia `.env.example` a `.env`. Genera contraseñas diferentes con `openssl rand -hex 24` para PostgreSQL y el worker MQTT. Define ADMIN_EMAIL, ADMIN_NAME y ADMIN_PASSWORD (mínimo 12 caracteres).
2. Ejecuta `docker compose up -d --build`.
3. Ejecuta `docker compose run --rm admin`. No hay registro público ni credenciales predeterminadas. El comando no sobrescribe una cuenta existente.
4. Abre `https://localhost:8443`. Caddy usa una CA local: instala/confía explícitamente su certificado en el equipo de desarrollo; no desactives la validación TLS.
5. Borra ADMIN_PASSWORD de `.env` después del alta inicial. Las contraseñas de usuarios se restablecen desde Administración; esto revoca sus sesiones.

Extraer el certificado público para confiar en la CA local:

```sh
docker compose cp proxy:/data/caddy/pki/authorities/local/root.crt ./firefly-local-ca.crt
```

Los puertos HTTPS y MQTT TLS se enlazan a 127.0.0.1 de forma predeterminada. Para una instalación de red, configura WEB_BIND, MQTT_BIND, SITE_ADDRESS, PUBLIC_ORIGIN y MQTT_HOST; configura certificados confiables y firewall de la instalación. La aplicación siempre requiere autenticación. El perfil de certificados incluido es **para instalación privada/local**, no un despliegue con certificados públicos automáticos.

PostgreSQL y MQTT sin TLS solo escuchan dentro de la red Compose. No se publica el puerto 1883. Para gateways externos usa TLS 8883 y su certificado/CA correspondiente. Cambiar MQTT_HOST requiere regenerar o sustituir los certificados del broker, no borrar los volúmenes de datos de la aplicación.

## Datos del broker al registrar un gateway

El alta abre una ventana con URL, puerto TLS, usuario, clave de entrega única, client ID, topics y certificado. Permite copiar o descargar JSON, PEM y un bloque **AT propuesto para el futuro firmware**. No es compatible con la consola actual del C6. La ventana se abre también al renovar una clave; el botón Conexión permite volver a consultar los datos sin recuperar el secreto.

`MQTT_PUBLIC_HOST` y `MQTT_PUBLIC_PORT` definen la dirección anunciada. La web advierte si es localhost, el broker solo escucha en loopback o el certificado no corresponde al host. La autenticación verificada desde el servidor no equivale a disponibilidad desde el C6. La configuración local no se expone a la LAN automáticamente.

[Configuración LAN, certificado y contrato AT provisional](plan/registro-gateway.md).

## Operación

1. Crea sedes, edificios y niveles en Administración.
2. Registra gateways. El identificador MQTT es también su usuario; la contraseña generada solo se muestra al crearlo. Una rotación se hace introduciendo una nueva contraseña en la edición.
3. El worker sincroniza las credenciales en pocos segundos. Cada gateway solo publica sus datos y recibe sus propias órdenes. No comparte la cuenta del worker.
4. Las lámparas se descubren al recibir telemetría. Asígnales nombre, tensión nominal, nivel y lugar desde su ficha.
5. Carga DXF ASCII 2D de hasta 5 MiB. Se admiten LINE, POLYLINE, LWPOLYLINE, CIRCLE y ARC; se avisa de entidades omitidas. Textos no se dibujan y bulges se aproximan. Bloques INSERT/XREF deben explotarse previamente. No es un editor CAD.
6. Un plano nuevo se guarda como borrador. Revisa las posiciones de todas las lámparas y gateways y activa explícitamente el plano. Un monitor solo ve los activos.
7. Crea perfiles según la batería y su documentación. Los valores de referencia de 6 V en el formulario no son recomendaciones para otras baterías. Cada edición crea una versión; no la transmite automáticamente.
8. En la ficha selecciona una versión compatible y solicita su aplicación. Pendiente/enviada/diferida no equivale a aplicada. Solo un ACK del nodo confirma la aplicación. Los nodos sin capacidad declarada rechazan esta operación en la API.

Administrador: configuración y consulta. Monitor: consulta, PDF y CSV. Sin sesión no se accede a API, reportes, planos ni SSE. La sesión dura 12 horas y usa cookie HttpOnly/SameSite; mutaciones exigen Origin válido. Login limitado a 10 intentos por cuenta/dirección cada 15 minutos.

## Fechas, estados y reportes

En **Planos**, selecciona un nivel, cambia «Tipo de dispositivo» a «Gateways», selecciona el equipo y pulsa «Colocar o mover gateway en este nivel». Haz clic en el plano para guardar su posición. En un borrador, las posiciones se guardan juntas al activarlo. La ubicación física del gateway puede ser distinta del nivel de las lámparas que atiende. Solo el administrador coloca o mueve dispositivos.

Las lámparas usan un círculo verde cuando hay telemetría reciente y gris cuando no hay datos, vencen tres intervalos esperados o el gateway está sin conexión. Activa significa comunicación, no luz encendida. El gateway usa un cuadrado con G, azul cuando comunica y gris cuando está desconectado. La última alerta se indica aparte con una insignia naranja, sin cambiar el color de comunicación. Los estados se actualizan en vivo; también vencen por tiempo si se interrumpe ese canal.

UTC en PostgreSQL y America/Lima en pantalla. La disponibilidad vence tras tres intervalos esperados. Alimentación, alertas, sensores y prueba son estados independientes. Una lectura histórica no demuestra conexión actual.

Los mensajes sin hora de medición fiable se identifican por fecha de recepción. El gateway debe marcar backlog como `live:false`. No se reconstruyen fechas antiguas a partir del uptime. Los datos actuales no retroceden por mensajes atrasados o arranques retirados.

Las muestras guardan una instantánea de la ubicación conocida **al recibirlas**, y del perfil correspondiente a la revisión declarada, si es conocida. Para datos retransmitidos no se inventa una ubicación de origen anterior. La estimación porcentual del firmware no mide capacidad ni autonomía; un flag de alerta tampoco acredita iluminación real. Resultados sin perfil confirmado se conservan como originales con diagnóstico limitado.

Reportes: mismo filtro para vista previa y descargas. La vista previa muestra hasta 100 filas por tipo; las exportaciones hasta 50.000 por tipo y rechazan rangos mayores, sin truncado silencioso. El PDF usa la plantilla mensual proporcionada: cabecera con el SVG Firefly, gráficos, tabla de doce meses y resumen; agrega anexos por lámpara, ubicación, revisión y prueba. El rango máximo del PDF es de 12 años; el CSV exporta filas completas con origen temporal. Mediciones y resultados se descargan en archivos CSV separados. El worker elimina mediciones de más de 12 meses; los resultados se conservan.

## Simulación opcional

Nunca se activa automáticamente. Los registros y perfiles se identifican como SIMULACIÓN y no deben usarse en hardware.

Para desarrollo local con Node >=22.21:

```sh
npm ci
# Con Compose levantado, publique temporalmente puertos de DB/MQTT solo en loopback:
# vea el ejemplo local en plan/verificacion.md
node scripts/local.mjs scripts/demo.mjs
node scripts/local.mjs scripts/run-simulator.mjs
```

También puede usarse el servicio Compose `simulator` con perfil `simulation`: registre `SIM_GATEWAY` desde la web y configure su `SIM_PASSWORD` en `.env`, después `docker compose --profile simulation up -d simulator`. Genera dos dispositivos sintéticos y persiste revisiones en su volumen. No controla lámparas reales.

## Desarrollo y verificación

```sh
npm test
npm run check
npm run build
```

`npm test` ejecuta pruebas unitarias; las de integración se omiten salvo activación explícita. Con PostgreSQL y servidor local en 4300: `INTEGRATION=1 node scripts/local.mjs --test tests/integration.test.mjs`. Para TLS/ACL del broker tras preparar demo y extraer su certificado: `MQTT_TEST=1 node --test tests/mqtt.test.mjs`.

La batería de integración crea y limpia fixtures identificados por un UUID. No debe ejecutarse contra una instalación de producción. Evidencia y límites: [verificación](plan/verificacion.md).

## Respaldo y restauración

Desde esta carpeta: `sh scripts/backup.sh`. Protege la carpeta resultante: contiene datos privados y credenciales/certificados del broker. DXF originales, geometría, posiciones, usuarios, perfiles y datos están en PostgreSQL y quedan en el mismo respaldo transaccional. Los volúmenes adicionales conservan autenticación y certificados.

`sh scripts/restore-check.sh backups/<fecha>` valida checksums, restaura en una **base nueva** y extrae archivos en una carpeta aislada; no modifica la base activa. Para recuperación operativa se debe detener web/worker, restaurar en una base nueva, cambiar DATABASE_URL y recuperar los certificados del respaldo antes de arrancar. No usar `docker compose down -v` para reiniciar: elimina los datos.

## Documentación técnica

- [Plan y requisitos](plan/README.md)
- [Arquitectura / contrato MQTT](plan/arquitectura.md)
- [Integración firmware pendiente](plan/firmware.md)
- [Tareas](plan/tareas.md)

No se realizó commit, push ni publicación. Los archivos `.env`, `.runtime`, dependencias y respaldos están ignorados por Git.

El diseño y el significado de los indicadores se documentan en [plantilla-reportes.md](plan/plantilla-reportes.md). Los meses sin muestras quedan vacíos; no se inventan metas ni se promedian magnitudes con unidades diferentes.

## Acceso desde la red local

Instalación actual: **https://192.168.18.86:8443**. La web escucha en `0.0.0.0:8443`; los clientes usan la IP real del servidor, no `0.0.0.0`. El login sigue siendo obligatorio.

Configuración local en `.env` (no se versiona):

```dotenv
PUBLIC_ORIGIN=https://192.168.18.86:8443
SITE_ADDRESS=192.168.18.86
WEB_BIND=0.0.0.0
```

Después de un cambio, ejecutar `docker compose up -d`. Si solo se edita `docker/Caddyfile`, recargar con `docker compose exec proxy caddy reload --config /etc/caddy/Caddyfile --adapter caddyfile`.

HTTPS usa la CA interna persistente de Caddy. Su certificado público se entrega en `firefly-local-ca.crt` (ignorado por Git). Transfiérelo por un medio de confianza e instálalo como CA de confianza en cada dispositivo o navegador cliente. No es una clave privada. Puede volver a extraerse con:

```sh
docker compose cp proxy:/data/caddy/pki/authorities/local/root.crt firefly-local-ca.crt
```

Caddy configura [`default_sni`](https://caddyserver.com/docs/caddyfile/options#default_sni) para permitir HTTPS por IP a clientes sin SNI a través de Docker. Reservar la IP del servidor en el DHCP del router evita cambios de dirección; si cambia, actualizar tanto origen como dirección de Caddy y recrear servicios. El puerto 8443 debe ser alcanzable entre los dispositivos de la LAN. No se configura reenvío de puertos en el router. PostgreSQL y MQTT sin TLS quedan dentro de Docker; el broker TLS mantiene su configuración de exposición independiente.

## Papelera de inventario

Administración permite **Enviar a papelera** en Ubicaciones (sedes, edificios y niveles), Gateways y Lámparas. El envío requiere confirmar el registro. La pestaña **Papelera** muestra los retirados y permite **Restaurar**; solo el administrador tiene acceso. No hay borrado definitivo.

Retira primero las lámparas de un gateway y los dispositivos/ubicaciones dependientes de una ubicación. La aplicación explica el bloqueo si aún hay dependencias. Para restaurar, empieza por sede → edificio → nivel → gateway → lámparas. Un elemento retirado conserva su identidad, ubicación e historial; sus identificadores siguen reservados para permitir la restauración. Los planos de un nivel retirado vuelven a estar disponibles al restaurarlo.

Los retirados no aparecen en inventario, supervisión ni planos. La telemetría no restaura lámparas automáticamente. Un gateway retirado deja de aceptar datos en la aplicación y el worker retira sus credenciales para nuevas conexiones MQTT (actualización cada tres segundos); una conexión MQTT ya establecida puede permanecer conectada, pero sus mensajes se rechazan. Al restaurarlo, sus credenciales vuelven a habilitarse si el gateway estaba activo. Las órdenes pendientes de una lámpara se cancelan al retirarla; si se necesita configuración después de restaurar, debe solicitarse de nuevo.

Las mediciones y resultados ya almacenados siguen disponibles en reportes históricos, con la política de conservación existente. La papelera no cambia los plazos de conservación de mediciones.

## Prueba individual de batería

En la ficha de una lámpara, el administrador puede pulsar **Ejecutar prueba individual · 30 s** y confirmar. La ficha muestra solicitud, confirmación del gateway y resultado recibido. Un envío MQTT o la aceptación del C6 no equivale a PASS.

Se habilita con comunicación reciente, AC, sensor válido y un C6 que anuncie `test_capable:true`. El firmware C6 nuevo debe cargarse a la placa y sincronizar su reloj; esta etapa solo compila el firmware, no lo carga. Firmware anterior mantiene el botón deshabilitado con una explicación. No requiere cambios del formato Zigbee ni del firmware H2 existente.

Solo una prueba individual pendiente por gateway. La orden vence a los 30 segundos si no llega al C6 y se espera resultado hasta tres minutos. No se reenvían órdenes físicas automáticamente. Un envío incierto bloquea nuevas solicitudes hasta el vencimiento; un resultado correlacionado que llegue tarde puede resolverlo. Las pruebas activas impiden enviar su registro a papelera. Las mediciones y resultados quedan en los reportes habituales.

El C6 valida UUID, nodo, duración fija, reloj y vencimiento; registra en NVS antes de enviar por Zigbee y no repite la orden tras un reinicio o duplicado MQTT. Sus campañas automáticas usan IDs 1–32767 y las pruebas web 32768–65535, sin reutilización por gateway en la base. La ejecución física y las condiciones de aceptación siguen en el H2. Los temporizadores de campaña y prueba remota evitan solaparlas en el C6; una orden puede rechazarse si una campaña o ciclo repetitivo ya está ocupado.

Contrato y límites físicos: [pruebas-individuales.md](plan/pruebas-individuales.md).

El worker sincroniza la política `docker/acl` hacia la copia privada que usa Mosquitto y recarga el broker al cambiarla, aunque las contraseñas sigan iguales. Se monta en el worker como `/broker-policy/acl:ro`; no basta con cambiar el archivo fuente y enviar SIGHUP a una copia antigua.
