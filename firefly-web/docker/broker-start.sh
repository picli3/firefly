#!/bin/sh
set -eu
if [ ! -f /mosquitto/auth/passwords ]; then
  mosquitto_passwd -b -c /mosquitto/auth/passwords firefly-worker "$MQTT_PASSWORD"
fi
cp /mosquitto/config/acl /mosquitto/auth/acl
chown mosquitto:mosquitto /mosquitto/auth/passwords /mosquitto/auth/acl
chmod 640 /mosquitto/auth/passwords /mosquitto/auth/acl
exec /usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf
