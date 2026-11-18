# FW-002 — Cambiar la conexión del C6 al broker de la Orange Pi

- **Estado:** pendiente de reconfigurar C6; servidor Orange Pi instalado y verificado.
- **Responsable:** chat de firmware C6/H2.
- **Fecha:** 2026-11-18, America/Lima.
- **Alcance:** configuración de conexión del C6; no modificar lógica H2 ni cambiar identidades de lámparas.

## Contexto

FireFly está instalado en la Orange Pi `192.168.18.40`, con archivos y datos persistentes en `/mnt/nvme/firefly`. La instalación de la PC continúa recibiendo datos mientras se verifica la nueva. La web y el broker nuevos están verificados; el corte de los dispositivos y la conciliación del histórico siguen pendientes.

La web instalará un broker MQTT TLS propio en la Orange Pi. Un C6 que siga apuntando al broker de la PC no enviará telemetría al nuevo servidor. La copia inicial de PostgreSQL es una instantánea, no una réplica continua.

## Datos de conexión previstos

- Broker anterior: `192.168.18.86:8883`.
- Broker nuevo: `mqtts://192.168.18.40:8883`.
- `gateway_id`, `device_id`, usuario y topics se conservan.
- Las contraseñas de gateways se copian como hashes en PostgreSQL; el servidor no recupera su texto original. Conservar la contraseña ya configurada en el C6. Si se perdió, coordinar explícitamente una renovación en la nueva web y actualizar la placa.
- Certificado público del nuevo broker: `/mnt/nvme/firefly/data/broker-public/ca.pem` en la Orange Pi, disponible después de inicializar certificados. Obtenerlo por SSH o desde los datos del broker en la nueva web; **no reutilizar el certificado del servidor anterior si no corresponde al nuevo endpoint**.
- Web nueva: `https://192.168.18.40:8443`.

## Tareas del chat de firmware

- [x] Despliegue web y broker nuevos verificados por el chat web, sin actuación física.
- [ ] Actualizar URI/host y confianza TLS del C6, manteniendo validación de certificado y sincronización de reloj.
- [ ] Conservar identidad, contraseña existente, topics y pruebas individuales/configuración que ya funcionen.
- [ ] Coordinar el instante de corte: no hacer pruebas físicas ni cargar firmware automáticamente solo por leer este archivo.
- [ ] Verificar en la nueva web recepción de las dos lámparas, estado del gateway y resultado de operaciones autorizadas.
- [ ] Informar cuándo puede dejar de usarse el servidor anterior y cómo se resolverán las mediciones recibidas allí después de la instantánea.

## Devolución

- Estado del nuevo servidor: verificado en ARM64; HTTPS, login, datos importados, reportes y MQTT TLS correctos.
- C6 reconfigurado/cargado: no realizado por el chat web.
- Telemetría en Orange Pi: pendiente.
- Corte definitivo y tratamiento de datos posteriores a la copia: pendiente de coordinación.

Copia local del certificado público para el chat de firmware: `firefly-web/.runtime/orangepi-install/broker-ca.pem` (ignorado por Git). Verificar que coincida con el certificado del broker de la Orange Pi antes de configurarlo. No contiene clave privada.

Huella SHA-256 del certificado MQTT verificado: `1D:10:CF:24:CF:15:B1:40:F1:0D:79:35:54:02:8D:DF:ED:E4:38:D4:9C:F6:05:8A:06:D5:04:27:B7:51:96:B5`. SAN: IP `192.168.18.40` y DNS `localhost`.
