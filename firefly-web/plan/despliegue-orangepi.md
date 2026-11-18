# Despliegue de FireFly en Orange Pi

Fecha de inicio: 2026-11-18. **Estado: instalado y verificado en ARM64. Pendiente únicamente el cambio de conexión del C6 y el corte definitivo con la PC.**

Destino: Orange Pi ARM64 en `192.168.18.40`. NVMe ext4 montado por UUID en `/mnt/nvme`.

## Organización prevista

```text
/mnt/nvme/firefly/
├── app/                   # Nuxt, servicios, scripts, Docker y documentación
│   ├── .env -> ../config/firefly.env
│   └── compose.override.yaml -> ../config/compose.override.yaml
├── config/                # 0700; variables/secretos, override y CA de confianza
├── data/                  # PostgreSQL, Mosquitto, certificados y Caddy
│   ├── postgres/
│   ├── broker-auth/
│   ├── broker-data/
│   ├── broker-certs/
│   ├── broker-public/
│   ├── caddy-data/
│   ├── caddy-config/
│   └── sim-state/
├── backups/               # 0700; instantánea importada y respaldos posteriores
├── logs/                  # 0700; registros de instalación/compilación
└── docs/                  # Guía operativa local
```

`docker/compose.orangepi.yaml` se instala como override local. Los volúmenes de FireFly usan bind mediante driver local y directorios del NVMe. No se cambia la raíz Docker global ni los datos de aplicaciones ajenas: sus imágenes y capas siguen bajo el almacenamiento Docker existente.

## Inicio automático

Unidad prevista: `docker/orangepi/firefly-web.service`, instalada en `/etc/systemd/system/firefly-web.service`. Requiere Docker y el montaje NVMe, y ejecuta Compose sin recompilar en cada arranque. No debe iniciar sobre un directorio vacío de la SD si falta la M.2.

## Datos y corte de conexión

Copiar usuarios, inventario, planos, perfiles y reportes mediante `pg_dump`/`pg_restore`. Mantener la instalación de la PC mientras se verifica la Orange Pi. El respaldo es una instantánea; no hay sincronización continua entre ambas bases. Las solicitudes físicas pendientes de la copia no deben ejecutarse de nuevo en el destino. Los dispositivos deben mostrarse desconectados hasta recibir mensajes reales en el nuevo broker.

La conexión del C6 la resuelve el otro chat mediante [FW-002](firmware/FW-002-migracion-broker-orangepi.md). No editar ni cargar firmware desde el chat web. El broker nuevo tendrá certificado para la IP de la Orange Pi; la confianza MQTT del C6 debe coincidir con ese certificado.

## Verificación pendiente

- [x] Directorios y permisos creados sobre el NVMe.
- [x] Aplicación ARM64 compilada.
- [x] Instantánea y certificados web importados, o estado inicial documentado.
- [x] Servicios activos con datos persistentes en el NVMe.
- [x] Login, protección de acceso, planos y descargas funcionando por HTTPS.
- [x] Broker TLS, autenticación y ACL verificados sin actuación física.
- [x] Arranque systemd habilitado y dependiente del montaje.
- [x] Guía y respaldos disponibles.
- [ ] C6 conectado al nuevo broker y corte coordinado con firmware.

## Resultado verificado

- Endpoint web: `https://192.168.18.40:8443`, mismas cuentas y contraseñas de la copia de origen.
- Imagen nativa `linux/arm64`; servicios web, worker, Mosquitto, PostgreSQL y Caddy activos.
- Copia con 1 gateway activo, 2 lámparas, 3 ubicaciones y 3 planos. Histórico y perfiles restaurados por pg_restore; no se creó una base vacía de reemplazo en la PC.
- Sin sesión: API, eventos, planos y reportes devuelven 401. Login, consultas, DXF, CSV y PDF correctos.
- MQTT TLS: validación de certificado para la IP nueva, autenticación y suscripción verificadas sin publicar órdenes físicas.
- Datos PostgreSQL comprobados dentro del contenedor sobre `/dev/nvme0n1p1` de 458 GB; driver local apunta a `/mnt/nvme/firefly/data/postgres`.
- Consumo medido en reposo del conjunto: aproximadamente 194 MiB de RAM; no es una prueba de carga.
- Unidad `firefly-web.service` habilitada y activa, con dependencia explícita del montaje.
- Guía operativa: `/mnt/nvme/firefly/docs/OPERACION.md`.
- Respaldo inicial: `/mnt/nvme/firefly/backups/initial-verified/`, con dump, certificados y checksums.
- Se preserva la CA web existente; el broker MQTT nuevo tiene certificado propio para la Orange Pi.

Instantánea PostgreSQL exportada: **2026-11-18 07:56:01 -05**. Las mediciones que sigan llegando a la PC después de ese momento no se sincronizan automáticamente con esta copia. Deben coordinarse durante FW-002 antes de retirar el origen.

Las sesiones importadas se invalidaron. Gateways quedan desconectados y sin capacidades confirmadas hasta reportar al nuevo broker. Solicitudes de prueba pendientes se cancelaron o marcaron inciertas, y configuraciones pendientes se dejaron sustituidas **solo en el destino** para no ejecutar órdenes antiguas de una instantánea. La base y el servicio de la PC permanecen activos e intactos.

Verificación posterior: se reinició únicamente `firefly-web.service`, se mantuvieron los datos y volvieron a pasar login, DXF, CSV, PDF y conexión MQTT TLS. Checksums del respaldo inicial correctos. No se reinició el servidor como parte del despliegue.
