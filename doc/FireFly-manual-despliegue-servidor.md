> [!portada]
> ![[logo-firefly.png]]
>
> # Manual de despliegue del servidor FireFly
> ## Instalación, actualización y mantenimiento
>
> ---
>
> **Maykol Rey**
> 2026
> Versión 1.0

> [!indice] Índice
> ```table-of-contents
> style: nestedList
> minLevel: 2
> maxLevel: 3
> includeLinks: true
> ```

Este manual explica cómo instalar el servidor FireFly desde cero, cómo actualizarlo a una versión nueva y cómo mantenerlo: respaldos, certificados y diagnóstico.

**Sirve tanto para ARM64 como para AMD64 (x86-64):** los comandos son idénticos en ambas arquitecturas (ver sección 2.1).

Los ejemplos usan la instalación de referencia: una **Orange Pi** (ARM64, Ubuntu 22.04) con disco **NVMe** montado en `/mnt/nvme` y la IP `192.168.18.40`. En un PC o servidor AMD64 solo cambian la ruta del disco de datos y el usuario del sistema. Sustituye `<IP_SERVIDOR>` por la dirección de tu servidor.

> **Regla de oro:** nunca ejecutes `docker compose down -v`. La opción `-v` borra los volúmenes, es decir, la base de datos, las contraseñas de los gateways y los certificados.

---

## 1. Qué se instala

El servidor es un conjunto de contenedores Docker definido en `firefly-web/compose.yaml`:

| Servicio             | Función                                                                                                                  | Puerto expuesto en el host     |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------ | ------------------------------ |
| `proxy` (Caddy)      | HTTPS de la aplicación web, con CA interna                                                                               | **8443/TCP** (`WEB_BIND`)      |
| `web` (Nuxt)         | Aplicación web y API                                                                                                     | ninguno (solo detrás de Caddy) |
| `worker`             | Ingesta MQTT, planificador de pruebas, envío de órdenes, sincronización de credenciales del broker y depuración de datos | ninguno                        |
| `mosquitto`          | Broker MQTT con TLS para los gateways                                                                                    | **8883/TCP** (`MQTT_BIND`)     |
| `db` (PostgreSQL 17) | Base de datos                                                                                                            | ninguno                        |
| `certs`              | Genera el certificado TLS del broker la primera vez y termina                                                            | —                              |
| `migrate`            | Aplica las migraciones de base de datos pendientes antes de arrancar la web y termina                                    | —                              |
| `admin`              | Herramienta para crear el primer administrador (perfil `tools`)                                                          | —                              |
| `simulator`          | Gateway simulado, solo para pruebas (perfil `simulation`)                                                                | —                              |

PostgreSQL y el MQTT sin cifrar (1883) **no** se publican fuera de Docker.

## 2. Requisitos

**Equipo**
- Linux de 64 bits, ARM64 o AMD64 (x86-64); ver sección 2.1. La referencia es una Orange Pi con 8 núcleos y 11 GiB de RAM. En reposo, FireFly usa unos 200 MiB de RAM; con 2 GB hay margen suficiente.
- Almacenamiento persistente (SSD/NVMe recomendado). Cada lámpara ocupa como máximo unos **0,6 GB**, porque el worker elimina las mediciones de más de 12 meses (los resultados de pruebas se conservan). Para 50 lámparas, calcula unos 30 GB más los respaldos.
- **IP fija**: resérvala en el DHCP del router. La IP queda grabada en el certificado del broker y en la configuración de cada gateway.

**Software**
- Docker Engine con el plugin Compose v2 (`docker compose version` debe responder). Referencia: Docker 29.8 y Compose 5.5.
- `openssl`, `tar`, `ssh` y `sha256sum`, presentes en cualquier distribución.

**Red**
- 8443/TCP accesible desde los equipos de los usuarios.
- 8883/TCP accesible desde los gateways.
- Salida a Internet para descargar imágenes la primera vez, y NTP para los gateways (`pool.ntp.org` por defecto).
- No hace falta abrir puertos en el router hacia Internet. FireFly está pensado para la red local.

### 2.1 ARM64 y AMD64

FireFly funciona en las dos arquitecturas con el **mismo código, los mismos comandos y los mismos archivos de configuración**. Está verificado en ambas: la Orange Pi de producción ejecuta la imagen `arm64` y el PC de desarrollo, la `amd64`.

|                          | ARM64 (aarch64)                                                  | AMD64 (x86-64)                                                 |
| ------------------------ | ---------------------------------------------------------------- | -------------------------------------------------------------- |
| Equipos típicos          | Orange Pi, Raspberry Pi 4/5 de 64 bits, servidores ARM           | PC, mini PC (Intel N100, etc.), servidores, máquinas virtuales |
| `uname -m` responde      | `aarch64`                                                        | `x86_64`                                                       |
| Sistema operativo        | Linux de **64 bits** (un sistema de 32 bits, `armv7l`, no sirve) | Linux de 64 bits                                               |
| Compilación de la imagen | Varios minutos en una Orange Pi                                  | Más rápida                                                     |

Por qué funciona igual en las dos:
- Todas las imágenes base son multiarquitectura: `node:22-bookworm-slim`, `postgres:17-alpine`, `eclipse-mosquitto:2`, `caddy:2-alpine` y `alpine/openssl`. Docker descarga automáticamente la variante del equipo.
- La imagen de la aplicación (`firefly-web:local`) se **compila en el propio servidor** con `docker compose build`, así que siempre sale con la arquitectura correcta. No hay que pasar `--platform` ni usar emulación.

Lo único que **no** se puede hacer es copiar la imagen ya compilada (`docker save` / `docker load`) de un equipo a otro de distinta arquitectura. Siempre se copia el **código** (sección 4) y se compila en el destino.

Los respaldos sí son portables entre arquitecturas: `database.dump` es un volcado lógico de PostgreSQL y los volúmenes se guardan como `tar`. Un respaldo de la Orange Pi se restaura en un servidor AMD64, y al revés, con el procedimiento de la sección 11. Lo que **no** es portable es copiar a mano la carpeta interna de PostgreSQL (`data/postgres`) entre arquitecturas: usa siempre `backup.sh`.

En un servidor AMD64 sin Orange Pi, adapta estos tres puntos:
1. **Disco de datos**: usa la ruta que prefieras (por ejemplo, `/srv` o `/opt`) en lugar de `/mnt/nvme`. Ajusta `FIREFLY_DATA_DIR` y, en `firefly-web.service`, `WorkingDirectory`, `RequiresMountsFor` y `ConditionPathIsMountPoint`. Si los datos no están en un disco aparte, elimina `ConditionPathIsMountPoint` y `ExecStartPre`.
2. **Usuario**: cambia `orangepi:orangepi` por el usuario normal del servidor en los comandos `chown`.
3. **Nombre del override**: `compose.orangepi.yaml` no tiene nada específico de la Orange Pi; sirve igual en AMD64.

## 3. Preparar el servidor

### 3.1 Instalar Docker

En Ubuntu o Debian, usa el método oficial de Docker (repositorio `download.docker.com`) e instala `docker-ce`, `docker-ce-cli`, `containerd.io` y `docker-compose-plugin`. Comprueba con:

```sh
docker --version
docker compose version
systemctl is-enabled docker   # debe decir: enabled
```

### 3.2 Montar el disco de datos

Monta el disco por **UUID**, con `nofail` para que el sistema arranque aunque falte el disco:

```sh
blkid /dev/nvme0n1p1                 # anota el UUID
mkdir -p /mnt/nvme
echo 'UUID=<UUID> /mnt/nvme ext4 defaults,noatime,nofail,x-systemd.device-timeout=10s 0 2' >> /etc/fstab
mount -a && findmnt /mnt/nvme
```

### 3.3 Crear la estructura de directorios

```sh
cd /mnt/nvme
mkdir -p firefly/{app,config,data,backups,logs,docs}
mkdir -p firefly/data/{postgres,broker-auth,broker-data,broker-certs,broker-public,caddy-data,caddy-config,sim-state}
chmod 700 firefly/config firefly/backups firefly/logs
```

```text
/mnt/nvme/firefly/
├── app/       código de la aplicación (se reemplaza en cada actualización)
├── config/    0700 · secretos (.env), override de Compose y CA web
├── data/      PostgreSQL, broker, certificados y Caddy (NUNCA se borra)
├── backups/   0700 · respaldos
├── logs/      0700 · registros de instalación
└── docs/      notas operativas locales
```

Separar `app/` de `config/` y `data/` permite sustituir el código sin tocar secretos ni datos.

## 4. Copiar la aplicación

Desde el equipo de desarrollo, en la raíz del repositorio `Lamp-iot`, empaqueta la carpeta `firefly-web` de la versión confirmada (`HEAD` de `main`) y descomprímela en `app/` del servidor:

```sh
git status --short firefly-web     # debe estar vacío: solo se despliega lo confirmado
git archive --format=tar --prefix=app/ HEAD:firefly-web \
  | ssh root@<IP_SERVIDOR> 'tar -x -C /mnt/nvme/firefly && chown -R orangepi:orangepi /mnt/nvme/firefly/app'
```

`git archive` incluye solo los archivos versionados, así que no copia `node_modules`, `.env`, respaldos ni certificados locales. El `chown` es opcional: usa el usuario normal del servidor (en la Orange Pi, `orangepi`).

## 5. Configurar las variables de entorno

Crea `config/firefly.env` a partir de la plantilla y enlázalo desde `app/`:

```sh
cd /mnt/nvme/firefly
cp app/.env.example config/firefly.env
chmod 600 config/firefly.env
ln -s ../config/firefly.env app/.env
openssl rand -hex 24   # ejecútalo dos veces: una clave para PostgreSQL y otra para MQTT
```

Edita `config/firefly.env`:

| Variable                                      | Valor en la referencia       | Para qué sirve                                                                                       |
| --------------------------------------------- | ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| `POSTGRES_PASSWORD`                           | *(aleatoria)*                | Clave de la base de datos. No cambiarla después del primer arranque.                                 |
| `MQTT_PASSWORD`                               | *(aleatoria)*                | Clave del worker en el broker.                                                                       |
| `PUBLIC_ORIGIN`                               | `https://192.168.18.40:8443` | URL exacta con la que los usuarios abren la web. Protege contra peticiones de otros orígenes.        |
| `SITE_ADDRESS`                                | `192.168.18.40`              | Nombre o IP del certificado HTTPS de Caddy.                                                          |
| `WEB_BIND`                                    | `0.0.0.0`                    | Interfaz en la que escucha la web. `127.0.0.1` la deja solo para el propio servidor.                 |
| `HTTPS_PORT`                                  | `8443`                       | Puerto HTTPS.                                                                                        |
| `MQTT_HOST`                                   | `192.168.18.40`              | IP o nombre que se escribe en el certificado del broker.                                             |
| `MQTT_PUBLIC_HOST`                            | `192.168.18.40`              | Dirección que la web entrega a los gateways al registrarlos.                                         |
| `MQTT_PUBLIC_PORT` / `MQTT_PORT`              | `8883`                       | Puerto MQTT TLS.                                                                                     |
| `MQTT_BIND`                                   | `192.168.18.40`              | Interfaz en la que escucha el broker.                                                                |
| `GATEWAY_NTP_SERVERS`                         | `pool.ntp.org`               | Servidores de hora que se entregan a los gateways.                                                   |
| `FIREFLY_DATA_DIR`                            | `/mnt/nvme/firefly/data`     | Carpeta de datos (la usa el override del paso 6).                                                    |
| `ADMIN_EMAIL`, `ADMIN_NAME`, `ADMIN_PASSWORD` | —                            | Solo para crear el primer administrador (paso 7.3). La contraseña debe tener al menos 12 caracteres. |

`SIM_GATEWAY` y `SIM_PASSWORD` solo se usan con el simulador; en producción quedan vacías.

## 6. Guardar los datos en el disco (override)

Por defecto, Docker guarda los volúmenes en su propia carpeta. El override `docker/compose.orangepi.yaml` hace dos cosas: dirige cada volumen a `FIREFLY_DATA_DIR` y limita el tamaño de los registros (3 × 10 MB por servicio).

```sh
cd /mnt/nvme/firefly
cp app/docker/compose.orangepi.yaml config/compose.override.yaml
ln -s ../config/compose.override.yaml app/compose.override.yaml
```

Compose lee `compose.override.yaml` automáticamente. Si falta alguna carpeta de `data/`, Docker se niega a arrancar, y es lo correcto: así nunca se arranca con una base de datos vacía por error.

## 7. Primer arranque

### 7.1 Construir y levantar

```sh
cd /mnt/nvme/firefly/app
docker compose build                 # en una Orange Pi tarda varios minutos
docker compose up -d
docker compose ps
```

El arranque sigue este orden:
1. `certs` genera el certificado del broker para `MQTT_HOST`.
2. `db` queda lista.
3. `migrate` crea las tablas.
4. Arrancan `web`, `worker`, `mosquitto` y `proxy`.

`certs` y `migrate` deben aparecer como **Exited (0)**; el resto, como **running**.

### 7.2 Comprobar

```sh
docker compose logs --tail 50 migrate worker mosquitto
curl -sk -o /dev/null -w '%{http_code}\n' https://<IP_SERVIDOR>:8443/login    # 200
curl -sk -o /dev/null -w '%{http_code}\n' https://<IP_SERVIDOR>:8443/api/lamps  # 401 sin sesión
```

### 7.3 Crear el primer administrador

No hay registro público ni contraseñas predeterminadas.

```sh
docker compose run --rm admin
```

El comando lee `ADMIN_EMAIL`, `ADMIN_NAME` y `ADMIN_PASSWORD` del `.env`. Si la cuenta ya existe, no la modifica.

**Después, borra el valor de `ADMIN_PASSWORD` de `config/firefly.env`.** Las demás cuentas se crean desde *Administración* en la web.

### 7.4 Confiar en el certificado HTTPS

Caddy firma el HTTPS con su propia CA interna. Extrae el certificado **público** de esa CA:

```sh
docker compose cp proxy:/data/caddy/pki/authorities/local/root.crt ../config/firefly-web-ca.crt
```

Instala ese archivo como **autoridad de confianza** en cada PC, tablet o móvil que use la web, transfiriéndolo por un medio fiable. No es una clave privada. Si no lo instalas, el navegador mostrará un aviso de seguridad. No desactives la validación de certificados.

### 7.5 Arranque automático con systemd

```sh
cp /mnt/nvme/firefly/app/docker/orangepi/firefly-web.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now firefly-web.service
systemctl status firefly-web.service
```

La unidad espera a Docker y al montaje `/mnt/nvme`. Si falta el disco, no arranca. Levanta los servicios **sin recompilar** (`up -d --no-build`). `systemctl stop firefly-web` detiene solo FireFly. Si usas otra ruta, ajusta `WorkingDirectory` y `RequiresMountsFor`.

### 7.6 Respaldo inicial

```sh
cd /mnt/nvme/firefly/app
sh scripts/backup.sh /mnt/nvme/firefly/backups/inicial-$(date -u +%Y%m%dT%H%M%SZ)
```

## 8. Conectar los gateways

1. En la web, ve a **Administración → Gateways → Registrar gateway**. La ventana de conexión muestra el host, el puerto TLS, el usuario, una **contraseña que solo se muestra una vez**, el client ID, los topics y el certificado del broker (`ca.pem`). Descarga el JSON.
2. Conecta el gateway C6 por USB y carga la configuración:
   ```sh
   python3 firmware/c6-zigbee-gateway/tools/gateway_at.py load <archivo>.json --restart
   ```
3. En unos segundos, el worker da de alta las credenciales en el broker. El gateway aparece **en línea** y las lámparas se descubren solas con su primera telemetría.

Cada gateway tiene su propio usuario MQTT y solo puede publicar sus datos y leer sus órdenes (ACL en `docker/acl`).

## 9. Lista de verificación de la instalación

- [ ] `docker compose ps`: `db`, `web`, `worker`, `mosquitto` y `proxy` en *running*; `certs` y `migrate` en *Exited (0)*.
- [ ] `https://<IP_SERVIDOR>:8443/login` abre sin aviso de certificado en un equipo con la CA instalada.
- [ ] La API sin sesión responde 401.
- [ ] Puedes iniciar sesión con el administrador, y `ADMIN_PASSWORD` ya no está en el `.env`.
- [ ] `systemctl is-enabled firefly-web` dice `enabled`.
- [ ] `docker volume inspect firefly-web_database` apunta a `/mnt/nvme/firefly/data/postgres`.
- [ ] Hay un respaldo inicial en `backups/` y `sha256sum -c SHA256SUMS` lo da por correcto.
- [ ] Un gateway registrado aparece en línea y envía telemetría.

## 10. Actualizar a una versión nueva

Aplica este procedimiento cada vez que se integra un cambio en `main`. Los datos no se tocan: solo se sustituye el código y se reconstruye la imagen.

**1. Confirma que el código está listo** (en el equipo de desarrollo):
```sh
cd firefly-web && npm test && npm run build && cd ..
git status --short firefly-web     # vacío
git log -1 --oneline               # anota el commit que vas a desplegar
```

**2. Respalda el servidor:**
```sh
ssh root@<IP_SERVIDOR>
cd /mnt/nvme/firefly/app
B=/mnt/nvme/firefly/backups/pre-deploy-$(date -u +%Y%m%dT%H%M%SZ)
sh scripts/backup.sh "$B"
tar -czf "$B/app-files.tar.gz" --exclude=.env --exclude=compose.override.yaml .
docker tag firefly-web:local firefly-web:pre-$(date +%Y%m%d)   # imagen anterior, para volver atrás
```

**3. Copia el código nuevo** (desde el equipo de desarrollo, con el mismo comando del paso 4):
```sh
git archive --format=tar --prefix=app/ HEAD:firefly-web \
  | ssh root@<IP_SERVIDOR> 'tar -x -C /mnt/nvme/firefly && chown -R orangepi:orangepi /mnt/nvme/firefly/app'
```
La copia sobrescribe los archivos versionados y respeta `.env` y `compose.override.yaml`, que son enlaces a `config/`. Los archivos que se hayan eliminado del repositorio se quedan en el servidor; si hace falta limpiarlos, bórralos a mano.

**4. Reconstruye y reinicia:**
```sh
cd /mnt/nvme/firefly/app
docker compose build web
docker compose up -d
docker compose ps
docker compose logs --tail 30 migrate web worker
```
`web`, `worker`, `migrate` y `admin` comparten la imagen `firefly-web:local`, así que basta con construirla una vez. Al subir los servicios, `migrate` aplica las migraciones nuevas, por ejemplo `Migración aplicada: 005_test_schedules.sql`. Cada migración se aplica una sola vez y queda anotada en la tabla `migrations`.

**5. Haz las pruebas rápidas:** login 200, API sin sesión 401, `systemctl is-active firefly-web`, una lámpara con datos recientes y los registros del worker sin errores.

### Volver a la versión anterior

Si la versión nueva falla y **no aplicó migraciones** (el registro de `migrate` no muestra ninguna nueva):

```sh
cd /mnt/nvme/firefly/app
tar -xzf <respaldo>/app-files.tar.gz
docker tag firefly-web:pre-<fecha> firefly-web:local
docker compose up -d --no-build
```

Si **sí aplicó una migración**, el código anterior puede no entender la base nueva:
1. Detén `web` y `worker`.
2. Restaura `database.dump` del respaldo previo en una base nueva (ver sección 11).
3. Apunta a ella y vuelve a arrancar.

Las mediciones recibidas entre el respaldo y la vuelta atrás se pierden. Por eso conviene probar en local cualquier versión que traiga migraciones.

### Cambios que exigen un orden de despliegue

Si una versión cambia lo que se intercambia con el firmware (campos de `status`, `telemetry` u órdenes), despliega **primero el servidor** y después el firmware. El servidor valida de forma estricta: un firmware nuevo con campos desconocidos sería rechazado por un servidor antiguo. Cada tarea `plan/firmware/FW-NNN` indica su orden.

## 11. Respaldos y restauración

### Qué incluye un respaldo

`scripts/backup.sh <carpeta>` genera:

| Archivo | Contenido |
| --- | --- |
| `database.dump` | Toda la base de datos: usuarios, inventario, planos DXF, posiciones, perfiles, mediciones, pruebas y planificaciones |
| `broker-auth.tar.gz` | Contraseñas (hash) y ACL de los gateways en el broker |
| `broker-certs.tar.gz` | Certificado y **clave privada** del broker |
| `caddy-data.tar.gz` | CA interna y certificados HTTPS |
| `SHA256SUMS` | Sumas de comprobación |

El respaldo contiene secretos: guárdalo con permisos 0700 y mantén **una copia fuera del servidor**. La instalación no programa respaldos automáticos. Si quieres uno diario, añade una tarea `cron` que ejecute `backup.sh`, y borra los respaldos antiguos.

### Comprobar un respaldo sin riesgo

```sh
cd /mnt/nvme/firefly/app
sh scripts/restore-check.sh /mnt/nvme/firefly/backups/<carpeta>
```

El script valida las sumas, restaura la base en una **base nueva aislada** y extrae los archivos en `.runtime/`. No modifica la base activa.

### Recuperación real

1. `docker compose stop web worker`
2. Restaura el respaldo en una base nueva con `restore-check.sh`, o con `createdb` + `pg_restore`.
3. Cambia `DATABASE_URL` para que apunte a esa base, o renómbrala para que sustituya a la actual.
4. Si se perdieron los volúmenes, recupera `broker-auth`, `broker-certs` y `caddy-data` del respaldo **antes** de arrancar. Si no, los gateways no podrán autenticarse y los navegadores no confiarán en el HTTPS.
5. `docker compose up -d` y revisa la lista de la sección 9.

## 12. Certificados

| Certificado | Dónde | Validez | Qué pasa al renovarlo |
| --- | --- | --- | --- |
| CA web (Caddy) | `caddy-data`, copia pública en `config/firefly-web-ca.crt` | La CA raíz dura años; Caddy renueva solo los certificados intermedios y de sitio | Nada, mientras se conserve `caddy-data` |
| Broker MQTT | `data/broker-certs`, copia pública en `data/broker-public/ca.pem` | **365 días** (en la referencia, hasta el 18-11-2027) | **Hay que volver a cargar el `ca.pem` nuevo en cada gateway** |

Consulta la fecha de vencimiento del broker con:

```sh
openssl x509 -in /mnt/nvme/firefly/data/broker-public/ca.pem -noout -enddate -subject
```

Renueva el certificado **antes** de que venza, planificando una ventana para reconfigurar los gateways:

```sh
cd /mnt/nvme/firefly/app
docker compose run --rm -e ROTATE_BROKER_CERT=1 certs   # guarda el anterior en previous-<fecha>/
docker compose restart mosquitto
```

Después, carga el nuevo `ca.pem` en cada gateway, con el JSON de la ventana *Conexión* y `gateway_at.py`.

**Cambiar la IP del servidor** obliga a:
- actualizar `PUBLIC_ORIGIN`, `SITE_ADDRESS`, `MQTT_HOST`, `MQTT_PUBLIC_HOST` y `MQTT_BIND`;
- rotar el certificado del broker (lleva la IP dentro);
- ejecutar `docker compose up -d`;
- reconfigurar todos los gateways.

Por eso la IP fija es un requisito.

## 13. Operación diaria y diagnóstico

```sh
systemctl status firefly-web
cd /mnt/nvme/firefly/app
docker compose ps
docker compose logs --tail 100 web worker mosquitto
docker compose logs -f worker          # en vivo
df -h /mnt/nvme                        # espacio en disco
docker compose exec db psql -U firefly -c "SELECT pg_size_pretty(pg_database_size('firefly'));"
```

| Síntoma | Causa probable | Qué hacer |
| --- | --- | --- |
| El navegador avisa «conexión no segura» | Falta la CA web en ese equipo, o `SITE_ADDRESS` no coincide con la URL | Instala `firefly-web-ca.crt`, o corrige `SITE_ADDRESS`/`PUBLIC_ORIGIN` y ejecuta `docker compose up -d` |
| El login falla con «origen no válido» | `PUBLIC_ORIGIN` es distinto de la URL usada (IP, puerto o https) | Corrige `PUBLIC_ORIGIN` y ejecuta `docker compose up -d` |
| El gateway aparece desconectado | Red, credenciales o certificado del broker | `docker compose logs mosquitto`. Comprueba IP y puerto 8883, que el gateway no esté en la papelera y que su `ca.pem` sea el vigente |
| El gateway conecta pero no aparecen datos | Mensajes rechazados por validación (p. ej., firmware más nuevo que el servidor) | Revisa `docker compose logs worker` y actualiza primero el servidor |
| Una prueba programada no se ejecutó | El gateway estaba ocupado, desconectado o sin AC durante los 15 min de reintento | Ficha de la lámpara → *Planificación → Ejecuciones programadas*: se muestra el motivo |
| `web` no arranca tras actualizar | Falló una migración | `docker compose logs migrate`. Corrige o vuelve a la versión anterior (sección 10) |
| No arranca nada tras reiniciar | Falta el montaje NVMe | `findmnt /mnt/nvme`; `journalctl -u firefly-web` |
| Disco lleno | Respaldos acumulados | Borra respaldos antiguos de `backups/` (después de copiarlos fuera) |

## 14. Qué no hacer

- `docker compose down -v`: borra todos los datos.
- Borrar o reinicializar `data/`, ni siquiera «para empezar de cero», sin un respaldo comprobado.
- Cambiar `POSTGRES_PASSWORD` en el `.env` después del primer arranque: la base conserva la clave original.
- Desplegar con cambios sin confirmar (`git status` con archivos modificados): el servidor dejaría de corresponder a un commit conocido.
- Publicar 8443 u 8883 hacia Internet sin una revisión de seguridad específica.
- Dejar `ADMIN_PASSWORD` escrita en el `.env`.
- Ejecutar el simulador o las pruebas de integración contra la instalación de producción.

## Referencias

- `firefly-web/README.md`: funcionamiento de la aplicación, red local y papelera.
- `firefly-web/plan/despliegue-orangepi.md`: registro de la migración de la PC a la Orange Pi.
- `firefly-web/docker/orangepi/README.md`: operación en la Orange Pi.
- `firefly-web/plan/registro-gateway.md` y `firmware/PROTOCOL.md`: contrato MQTT de los gateways.
- `doc/FireFly-presentacion-solucion.md`: descripción general de la solución.
