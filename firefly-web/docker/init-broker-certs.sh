#!/bin/sh
set -eu
mkdir -p /certs /public
if [ "${ROTATE_BROKER_CERT:-0}" = 1 ] || [ ! -s /certs/server.key ] || [ ! -s /certs/server.crt ]; then
  if [ -s /certs/server.key ]; then
    previous="/certs/previous-$(date -u +%Y%m%dT%H%M%SZ)"
    mkdir -m 700 "$previous"
    cp /certs/server.key "$previous/"
    [ ! -f /certs/server.crt ] || cp /certs/server.crt "$previous/"
  fi
  case "$MQTT_HOST" in
    *:*|[0-9]*.[0-9]*.[0-9]*.[0-9]*) san="IP:$MQTT_HOST" ;;
    *) san="DNS:$MQTT_HOST" ;;
  esac
  openssl req -x509 -newkey rsa:3072 -nodes -keyout /certs/server.key.tmp \
    -out /certs/server.crt.tmp -days 365 -subj "/CN=$MQTT_HOST" \
    -addext "subjectAltName=$san,DNS:localhost"
  mv /certs/server.key.tmp /certs/server.key
  mv /certs/server.crt.tmp /certs/server.crt
fi
chown 1883:1883 /certs/server.key /certs/server.crt
chmod 600 /certs/server.key
chmod 644 /certs/server.crt
cp /certs/server.crt /public/ca.pem.tmp
chmod 644 /public/ca.pem.tmp
mv /public/ca.pem.tmp /public/ca.pem
