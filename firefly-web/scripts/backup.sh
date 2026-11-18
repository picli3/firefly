#!/bin/sh
set -eu
umask 077
folder="${1:-backups/$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p "$folder"
docker compose exec -T db pg_dump -U firefly -d firefly -Fc > "$folder/database.dump"
# DXF originals, geometry, positions and profiles are all transactional PostgreSQL data.
for volume in broker-auth broker-certs caddy-data; do
 docker run --rm -v "firefly-web_${volume}:/source:ro" alpine tar -C /source -czf - . > "$folder/$volume.tar.gz"
done
(cd "$folder" && sha256sum database.dump broker-auth.tar.gz broker-certs.tar.gz caddy-data.tar.gz > SHA256SUMS)
printf 'Respaldo creado en %s\n' "$folder"
