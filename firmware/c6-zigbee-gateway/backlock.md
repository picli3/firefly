# Backlog del gateway ESP32-C6

## Ethernet y PoE

Implementar despues de validar el ciclo completo:

```text
ESP32-H2 luminaria -> Zigbee -> ESP32-C6 coordinador -> UART0
```

### Pendiente

- Agregar Ethernet 10/100 mediante W5500 por SPI.
- Mantener Zigbee como radio nativo del ESP32-C6 y no inicializar Wi-Fi.
- Definir el pinout SPI cuando se conozca el modelo exacto de la placa C6.
- Integrar el driver `espressif/w5500` con `esp_eth` y `esp_netif`.
- Obtener direccion IP mediante DHCP y reportarla por UART0.
- Evaluar publicacion MQTT, API HTTP, NTP y actualizaciones OTA por Ethernet.
- Seleccionar un RJ45 con magneticos integrados compatible con el W5500.
- Seleccionar un modulo PoE PD aislado y certificado para IEEE 802.3af.
- Alimentar el circuito desde la salida regulada del modulo PoE.
- Definir protecciones, filtrado, presupuesto de potencia y aislamiento en la PCB.
- Evitar PoE pasivo para conservar compatibilidad y seguridad con switches PoE estandar.

### Criterios previos

- Confirmar emparejamiento estable entre el H2 y el C6.
- Confirmar recepcion del payload `emergency_telemetry_t` por UART0.
- Validar secuencia, voltaje, corriente, potencia, temperatura y flags recibidos.
- Medir estabilidad y perdida de mensajes durante una prueba prolongada.
