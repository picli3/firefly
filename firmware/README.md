# Firmware de la lampara de emergencia IoT

Este directorio contiene cuatro proyectos ESP-IDF independientes:

- `h2-emergency-light`: nodo Zigbee instalado dentro de la luminaria.
- `h2-coordinator`: coordinador Zigbee y puente UART.
- `s3-gateway`: gateway UART, Wi-Fi y MQTT/TLS.
- `c6-zigbee-gateway`: coordinador Zigbee y consola de telemetria por USB nativo.

La version de referencia es ESP-IDF v5.5. El protocolo entre dispositivos esta
documentado en `PROTOCOL.md`.

Las estructuras binarias y constantes compartidas por el H2 y el C6 viven en el
componente `common/include/emergency_protocol.h`. Los proyectos no deben duplicar
esas definiciones porque cualquier diferencia rompe la compatibilidad Zigbee.

Cada proyecto se configura y compila desde su propia carpeta. Antes de usar
`idf.py`, active el entorno:

```bash
source ~/esp/esp-idf/export.sh
```
