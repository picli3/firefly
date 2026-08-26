# ESP32-C6 - Gateway Zigbee por UART0

Forma la red Zigbee de la luminaria, recibe su telemetria y muestra cada mensaje
como texto legible en la consola UART0 de ESP-IDF. Esta primera version no usa
Wi-Fi, MQTT ni un segundo microcontrolador.

## Compatibilidad

- Coordinador Zigbee en canal 15.
- Perfil Home Automation `0x0104`.
- Endpoint `1`.
- Cluster privado `0xFC00` en rol cliente.
- Comando de telemetria `0x01`.
- Payload `emergency_telemetry_t` version 1 descrito en `../PROTOCOL.md`.
- Hasta 50 dispositivos hijos.
- Registro automatico de hasta 50 nodos a partir de su telemetria.
- Campanas automaticas con grupos de hasta 10 lamparas.
- Prueba bajo bateria de 30 s y pausa de 15 s entre grupos.

## Consola serial

Los mensajes se imprimen mediante `ESP_LOGI` por UART0 a 115200 baud. Use el
puerto serial correspondiente a su placa; normalmente aparece como
`/dev/ttyUSB0` cuando la placa incorpora un puente USB-UART.

Ejemplo:

```text
I (...) c6_zigbee_gateway: Node joined: short_address=0x1234
I (...) c6_zigbee_gateway: RX short=0x1234 node=89abcdef seq=7 V=6120_mV I=1060_mA P=6487_mW T=24.50_C SoH=78% AC=0 alert=0 UVLO=0 sensor_valid=1 self_test=1 uptime=95_s
I (...) c6_zigbee_gateway: TEST short=0x1234 node=89abcdef id=7 result=PASS V_before=6300_mV V_min=5980_mV I_avg=1040_mA I_peak=1080_mA P_avg=6219_mW T_max=25.10_C duration=30_s samples=6
```

## Compilar y grabar

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Si la placa aparece con otro nombre, reemplace `/dev/ttyUSB0`. Para salir del
monitor use `Ctrl+]`.

## Emparejar la luminaria H2

1. Grabe el C6 y abra `idf.py monitor`.
2. El C6 forma una red nueva en canal 15 y habilita el emparejamiento.
3. Reinicie o energice el H2 de la luminaria dentro de los primeros 180 segundos.
4. Espere `Node joined` en la consola del C6.
5. La luminaria reporta inmediatamente y luego segun su intervalo adaptativo.

El C6 conserva la red en NVS. En reinicios posteriores restaura la misma red y
vuelve a permitir emparejamientos durante 180 segundos.

## Pruebas automaticas

El C6 aprende la direccion corta y el `device_id` de cada H2 cuando recibe su
telemetria. Al comenzar una campana ordena los nodos por `device_id`, forma grupos
con un maximo exacto de 10 integrantes y envia automaticamente una agenda unicast
a cada lampara. No se requiere pulsar botones ni enviar ordenes individuales.

Configuracion predeterminada:

| Parametro | Valor |
|---|---:|
| Primer ciclo desde el arranque | 1440 min |
| Intervalo entre ciclos | 1440 min |
| Lamparas simultaneas | 10 |
| Duracion bajo bateria | 30 s |
| Pausa entre grupos | 15 s |
| Preparacion del primer grupo | 10 s |

Sin Ethernet, RTC o NTP, el C6 solo dispone de tiempo relativo. Por tanto, la
hora nocturna queda definida por su hora de arranque. Por ejemplo, si arranca a
las 02:00, la primera campana ocurre 24 horas despues y se repite diariamente a
esa hora. La futura integracion Ethernet permitira fijar una hora civil mediante
NTP sin cambiar el protocolo con los H2.

Para una validacion de banco, cambie temporalmente `Minutes from gateway boot to
first campaign` a 1 desde:

```bash
idf.py menuconfig
```

Ruta: `Automatic battery tests`. Espere primero una linea `Registered node` para
confirmar que el C6 conoce la lampara. Durante una campana, UART0 muestra la
asignacion, cada reporte y el resumen acumulado de aprobadas y fallidas.

Si el H2 habia pertenecido a otra red Zigbee, entre primero al proyecto
`../h2-emergency-light` y borre su flash antes de reintentar:

```bash
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

Ese comando borra tambien cualquier configuracion persistente del H2.
