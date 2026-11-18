# Registro y entrega de configuración del gateway

Implementado solo en `firefly-web/`. **No modifica el firmware del C6.** La consola actual del C6 no reconoce los comandos AT propuestos aquí.

## Alta y credenciales

Administración → Gateways → Nuevo. Al guardar, se abre una ventana modal accesible que entrega URL, host, puerto MQTT TLS, usuario, client ID, clave, topics, Ethernet/DHCP, reloj/NTP y certificado PEM con huella SHA-256. La clave no aparece en mensajes generales, auditoría, URLs ni almacenamiento del navegador.

El alta conserva solo el hash de la clave. El backend comprueba la autenticación real contra Mosquitto, esperando hasta ocho segundos a la sincronización del worker. Un estado `verified` acredita esa autenticación desde el servidor, **no** la conectividad del C6 ni el funcionamiento de su firmware. Si falla, el gateway permanece registrado y la clave se entrega igualmente con estado pendiente; se puede repetir la verificación desde la ventana sin crear otro registro.

La configuración JSON y el bloque AT contienen secretos. Guardarlos en un lugar privado. La contraseña solo se puede obtener al registrar o al establecer una nueva. El botón **Conexión** vuelve a mostrar los datos públicos del registro, sin reconstruir contraseñas ni paquetes incompletos. Para renovar un paquete completo, editar el gateway y establecer una contraseña nueva; esto invalida la anterior en nuevas conexiones.

Monitor y visitantes no acceden a los endpoints de aprovisionamiento ni al certificado de esta pantalla. La conexión HTTPS, sesión y validación Origin siguen siendo obligatorias.

## Dirección anunciada al C6

La dirección externa del broker es independiente de `MQTT_URL`, que es la conexión interna usada entre contenedores.

- `MQTT_HOST`: host/IP que debe cubrir el certificado TLS.
- `MQTT_PUBLIC_HOST`: host/IP que se muestra al configurar gateways.
- `MQTT_PUBLIC_PORT`: puerto anunciado; debe corresponder al puerto publicado o al encaminamiento de la instalación.
- `MQTT_BIND`: interfaz donde Docker publica el broker; por defecto solo loopback.
- `GATEWAY_NTP_SERVERS`: hasta tres servidores NTP separados por coma para el futuro firmware.

La pantalla detecta loopback, certificado ausente, host/IP que no corresponde al certificado y fechas de validez incorrectas. No anuncia disponibilidad en red solo porque las credenciales funcionen dentro de Compose.

Ejemplo de instalación LAN (requiere que el nombre resuelva al servidor):

```dotenv
MQTT_HOST=broker.firefly.lan
MQTT_PUBLIC_HOST=broker.firefly.lan
MQTT_PUBLIC_PORT=8883
MQTT_PORT=8883
MQTT_BIND=0.0.0.0
GATEWAY_NTP_SERVERS=ntp.firefly.lan
```

Cambiar estas variables no modifica automáticamente un certificado existente. Para renovarlo de forma explícita:

```sh
docker compose stop worker mosquitto
docker compose run --rm -e ROTATE_BROKER_CERT=1 certs
docker compose up -d --force-recreate mosquitto worker web
```

El servicio conserva una copia anterior de los certificados en el volumen privado. Los certificados nuevos se deben volver a cargar en los clientes existentes. Se admiten SAN de host DNS, IPv4 o IPv6. El contenedor web recibe **solo el certificado público**, en otro volumen; no recibe la clave privada TLS.

No se cambia el bind local de la instalación actual automáticamente. Abrir la interfaz y configurar nombres, rutas y firewall corresponde al despliegue LAN elegido por el administrador.

## Contrato AT propuesto: `firefly-at/v1-draft`

**No es Espressif ESP-AT y no está implementado en el C6.** Este contrato permitirá implementar posteriormente el receptor sin inventar opciones distintas a las exportadas por la web.

1. `AT+FFPROVBEGIN=N`: inicia una transacción temporal de N fragmentos.
2. `AT+FFPROVDATA=i,"BASE64"`: envía el fragmento i, empezando en cero, con hasta 192 caracteres base64. Cada línea ocupa como máximo 220 caracteres para este formato.
3. `AT+FFPROVCOMMIT="SHA256"`: verifica número y orden de fragmentos, decodifica base64, calcula SHA-256 de los bytes UTF-8 del JSON y valida todos los parámetros antes de persistir el conjunto.
4. `AT+FFPROV?`: consulta el resultado y parámetros no secretos; no debe devolver contraseña ni contenido de la clave.

Usar CRLF. Base64 transporta JSON sin introducir comillas/saltos de línea en el parser AT; **no cifra la clave**. El receptor futuro debe aplicar un límite total, timeout, tratamiento idempotente de fragmentos y rechazo de transacciones incompletas; no puede activar parcialmente una configuración fallida. Los comentarios de advertencia del archivo TXT descargado son documentación, no comandos a transmitir.

JSON: `schema_version:1`, `gateway_id`, `network`, `time` y `mqtt`. Contiene TLS obligatorio con validación, PEM de confianza, client ID, usuario, clave, QoS 1, keepalive, heartbeat y rutas de topics. El bloque AT nunca se genera sin clave, dirección válida y certificado correspondiente al host. La conectividad LAN puede seguir pendiente y se indica por separado.

## Verificación

Pruebas unitarias: reconstrucción exacta del JSON desde los fragmentos, checksum, índices, escapado de caracteres, ausencia de paquetes sin clave, SAN DNS/IP, mismatch y certificado ausente. Integración: alta, autenticación real en Mosquitto, renovación de clave, consulta sin recuperar secreto, acceso exclusivo de administrador y descarga del PEM. Control de tipos, compilación y comprobación visual del modal.

La descarga «Descargar configuración del C6 (.json)» aparece al inicio de la ventana de alta o renovación, separada del contrato AT. El archivo `<gateway_id>-c6-config.json` incluye metadatos de preparación y `config` con los parámetros MQTT, CA, credenciales, red DHCP, NTP y topics. Contiene la contraseña de entrega única; debe guardarse antes de cerrar. La importación por el firmware C6 continúa pendiente.
