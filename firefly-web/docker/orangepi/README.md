# Operación de FireFly en Orange Pi

Directorio: `/mnt/nvme/firefly`. Aplicación ARM64 en `app/`; secretos en `config/` (0700); datos en `data/`; respaldos en `backups/`; registros de instalación en `logs/`.

Los enlaces `app/.env` y `app/compose.override.yaml` apuntan a `config/`. El override utiliza volúmenes Docker con bind a directorios NVMe. No migrar la raíz Docker global ni tocar los servicios ajenos para actualizar FireFly.

## Operación

```sh
systemctl status firefly-web.service
cd /mnt/nvme/firefly/app
docker compose ps
docker compose logs --tail 100 web worker mosquitto
```

La unidad requiere que `/mnt/nvme` esté montado, inicia Docker como dependencia y levanta Compose sin compilar en cada arranque. `systemctl stop firefly-web` detiene solo FireFly. No usar `docker compose down -v` para una actualización.

Para respaldar usuarios, planos, inventario, perfiles, históricos y certificados:

```sh
cd /mnt/nvme/firefly/app
sh scripts/backup.sh /mnt/nvme/firefly/backups/manual-$(date -u +%Y%m%dT%H%M%SZ)
```

Los respaldos contienen secretos y claves; conservar permisos privados y una copia fuera del NVMe. Esta instalación no configura una tarea periódica de respaldo.

## Acceso

- Web: `https://192.168.18.40:8443`, con autenticación obligatoria.
- MQTT TLS: `mqtts://192.168.18.40:8883`, credenciales independientes por gateway.
- CA web pública: `config/firefly-web-ca.crt`.
- Certificado público de confianza del broker: `data/broker-public/ca.pem`.
- PostgreSQL y MQTT sin TLS no publican puertos en el host.

## Migración y firmware

La base importada es una instantánea de la PC, no una réplica continua. Los dispositivos se dejan desconectados y las órdenes pendientes no se reejecutan desde la copia. No detener ni borrar la instalación anterior hasta coordinar el corte y el tratamiento de mediciones posteriores a la copia.

El chat de firmware debe resolver `plan/firmware/FW-002-migracion-broker-orangepi.md`: cambiar dirección y confianza TLS del C6, manteniendo su identidad. El chat web no cambia ni carga firmware. No enviar pruebas físicas durante la verificación del servidor sin la autorización correspondiente.
