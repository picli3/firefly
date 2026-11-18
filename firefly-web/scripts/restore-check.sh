#!/bin/sh
# Never overwrites the running application database.
set -eu
folder="${1:?Indique carpeta de respaldo}"
(cd "$folder" && sha256sum -c SHA256SUMS)
target="firefly_restore_$(date -u +%Y%m%d%H%M%S)"
docker compose exec -T db createdb -U firefly "$target"
docker compose exec -T db pg_restore -U firefly -d "$target" --exit-on-error < "$folder/database.dump"
docker compose exec -T db psql -U firefly -d "$target" -c 'SELECT count(*) AS usuarios FROM users; SELECT count(*) AS planos FROM plans; SELECT count(*) AS muestras FROM measurements;'
assets="$(mktemp -d .runtime/restore-assets.XXXXXX)"
for volume in broker-auth broker-certs caddy-data; do
 mkdir -p "$assets/$volume"
 tar -xzf "$folder/$volume.tar.gz" -C "$assets/$volume"
done
test -s "$assets/broker-auth/passwords"
test -s "$assets/broker-certs/server.crt"
printf 'Archivos recuperados en %s\n' "$assets"
printf 'Restauración verificada en base aislada %s; la base activa no fue modificada.\n' "$target"
