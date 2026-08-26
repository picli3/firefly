# Firmware de la lampara de emergencia IoT

Este directorio contiene cuatro proyectos ESP-IDF independientes:

- `h2-emergency-light`: nodo Zigbee instalado dentro de la luminaria.
- `h2-coordinator`: coordinador Zigbee y puente UART.
- `s3-gateway`: gateway UART, Wi-Fi y MQTT/TLS.
- `c6-zigbee-gateway`: coordinador Zigbee y salida de telemetria por UART0.

La version de referencia es ESP-IDF v5.5. El protocolo entre dispositivos esta
documentado en `PROTOCOL.md`.

Cada proyecto se configura y compila desde su propia carpeta. Antes de usar
`idf.py`, active el entorno:

```bash
source ~/esp/esp-idf/export.sh
```
