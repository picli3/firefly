# ESP32-C6 - Gateway Zigbee, USB y Ethernet opcional

Forma la red Zigbee de la luminaria, recibe su telemetria y la guarda en un CSV
persistente en la flash interna. Permite descargarlo por USB Serial/JTAG y
mantiene los mensajes legibles de diagnostico. Puede publicar telemetria por
Ethernet W5500 y MQTT. No usa Wi-Fi.

## Estructura del codigo

| Modulo | Responsabilidad |
|---|---|
| `main.c` | Inicializacion de NVS y arranque de modulos |
| `ethernet_mqtt.c/.h` | W5500, DHCP, NTP, TLS, estado y telemetria MQTT |
| `gateway_settings.c/.h` | Configuracion de red versionada, SHA-256, NVS y defaults |
| `at_provisioning.c/.h` | Aprovisionamiento AT por fragmentos sin eco de secretos |
| `remote_config.c/.h` | Configuracion MQTT fragmentada y ACK reales del H2 |
| `app_config.h` | Limites de red y constantes del gateway |
| `node_registry.c/.h` | Registro, actualizacion y ordenamiento de nodos |
| `telemetry_log.c/.h` | Formato legible de telemetria y reportes por USB |
| `telemetry_csv.c/.h` | Cola de recepcion, SPIFFS, persistencia y estado del CSV |
| `csv_format.c/.h` | Columnas CSV y CRC32 de la transferencia |
| `csv_serial.c/.h` | Comandos y descarga por USB sin intercalar logs |
| `test_campaign.c/.h` | Ciclo horario, grupos, progreso y resumen de pruebas |
| `zigbee_gateway.c/.h` | Coordinador, emparejamiento y comandos Zigbee |

Los paquetes compartidos con el H2 estan definidos una sola vez en
`../common/include/emergency_protocol.h`.

## Compatibilidad

- Coordinador Zigbee en canal 15.
- Perfil Home Automation `0x0104`.
- Endpoint `1`.
- Cluster privado `0xFC00` en rol cliente.
- Comando de telemetria `0x01`.
- Telemetria v1 (24 bytes) y v2 (33 bytes) descritas en `../PROTOCOL.md`.
- Hasta 50 dispositivos hijos.
- Registro automatico de hasta 50 nodos a partir de su telemetria.
- Campanas automaticas con grupos de hasta 10 lamparas.
- Prueba bajo bateria de 300 s y pausa de 15 s entre grupos.

## Consola serial

Los logs y comandos usan USB Serial/JTAG nativo. En el ESP32-C6, D− es GPIO12
y D+ es GPIO13. Conecte un cable de datos al puerto USB nativo de la placa; en
Linux normalmente aparece como `/dev/ttyACM0`. Use `/dev/serial/by-id/` si hay
varios equipos conectados. El puente USB-UART (`/dev/ttyUSB0`) ya no recibe los
comandos de este firmware.

Ejemplo:

```text
I (...) c6_zigbee_gateway: Node joined: short_address=0x1234
I (...) c6_zigbee_gateway: RX short=0x1234 node=89abcdef seq=7 V=6120_mV I=1060_mA P=6487_mW T=24.50_C SoH=78% AC=0 alert=0 UVLO=0 sensor_valid=1 self_test=1 uptime=95_s
I (...) c6_zigbee_gateway: TEST short=0x1234 node=89abcdef id=7 result=PASS V_before=6300_mV V_min=5980_mV I_avg=1040_mA I_peak=1080_mA P_avg=6219_mW T_max=25.10_C duration=30_s samples=6
```

## Diagnostico de enlaces Zigbee

La consola USB separa el estado del evento NLME del codigo de red. Ejemplo:

```text
W (...) zigbee_gateway: NLME event=ESP_OK nwk_status=0x00 (NO_ROUTE_AVAILABLE) addr=0xa182
```

`addr` es la direccion corta asociada al aviso, no necesariamente el emisor
ni el salto que fallo. Compararla con `RX short=...` y `zigbee links` para
correlacionar el aviso con la telemetria y los enlaces observados.

En el terminal del USB nativo, enviar una linea terminada en Enter:

- `zigbee links`: muestra los vecinos directos del C6 con `rssi_dbm` y `lqi`.
- `zigbee links 0x1234`: pide la tabla de vecinos al nodo de direccion corta
  `0x1234`. La respuesta asincrona muestra el LQI visto por ese nodo. Si
  `remote_end` devuelve `next` distinto de cero, pedir la pagina siguiente con
  `zigbee links 0x1234 <next>`.

Cada linea identifica `observer` y `neighbor`: el LQI corresponde a esa
direccion de observacion. La tabla de vecinos no demuestra que una trama haya
seguido una ruta concreta. La consulta remota no incluye RSSI y ninguna de las
dos consultas mide el ruido ni calcula SNR. Los resultados salen por consola
sin cambiar el CSV ni la configuracion de las lamparas.

## Guardado y descarga del CSV

Se guarda solo la telemetria valida recibida con el comando Zigbee `0x01`.
Los resultados de pruebas `0x04` siguen mostrandose en consola, pero no se
incluyen en el CSV. El protocolo Zigbee y el firmware H2 no cambian.

La particion `telemetry` ocupa 2 MiB desde `0x175000`. Las particiones de NVS,
aplicacion y Zigbee mantienen sus posiciones. El proyecto configura flash de
8 MiB; no hay que borrar toda la flash para instalar esta actualizacion.
El flasheo normal actualiza tambien la tabla de particiones y conserva el CSV.
`erase-flash` elimina tanto CSV como red y configuracion persistente.

El archivo es `/telemetry/telemetry.csv`, con separador coma y fin de linea LF:

```csv
gateway_boot_id,received_ms,short_addr,device_id,sequence,voltage_mv,current_ma,power_mw,temperature_centi_c,soh_pct,flags,node_uptime_s
1,65000,0x1234,89abcdef,7,6120,1060,6487,2450,78,24,95
```

- `gateway_boot_id`: contador de arranques persistido en NVS. Reinicia si se borra NVS.
- `received_ms`: milisegundos desde el arranque del C6, capturados al recibir el mensaje.
- `short_addr` y `device_id`: direccion Zigbee e identificador de lampara en hexadecimal.
- Tension en mV, corriente firmada en mA, potencia en mW y temperatura firmada
  en centesimas de grado Celsius. `-32768` conserva el indicador de temperatura
  invalida del H2; `sensor_valid` se interpreta desde `flags`.
- `flags`: mascara decimal; bit 0 AC, bit 1 alerta, bit 2 UVLO, bit 3 sensor
  valido y bit 4 prueba activa. Se conservan incluso muestras con sensor invalido.
- `node_uptime_s`: tiempo desde el arranque de la lampara. No hay fecha civil sin RTC/NTP.

El limite conservador del archivo es 1400 KiB (1 433 600 bytes), dejando margen
para metadatos y recoleccion de espacio de SPIFFS. A unas 90 bytes por fila,
caben aproximadamente 15 900 mediciones: unas 11 dias con una lampara cada
60 segundos, o 5 horas con 50 lamparas a ese intervalo. Es una estimacion;
los intervalos adaptativos y la longitud de las filas cambian la autonomia.

Una tarea independiente drena una cola de 64 mensajes y sincroniza cada fila.
Al alcanzar el limite, se detiene el guardado y se conserva el archivo hasta
su borrado explicito. Ante errores de escritura tambien se detiene el guardado.
Zigbee y las campanas continuan. `csv status` muestra `queue_drops` (mensajes
no encolados), `write_errors` y `skipped` (mensajes no guardados por registro
detenido), acumulados durante el arranque actual.

Solo se formatea automaticamente una particion completamente vacia (`0xFF`).
Una particion corrupta se conserva y se informa `mounted=0`. Si un corte deja
una ultima fila incompleta, no se modifica el archivo: se detiene el guardado
y se permite descargar el prefijo de filas completas antes de borrarlo.
Una fila aun en cola o escritura puede perderse ante un corte de alimentacion;
se requiere validar la persistencia y recuperacion en la placa real.

Desde este directorio, prepare la herramienta de la computadora:

```bash
python3 -m venv /tmp/c6-csv-venv
/tmp/c6-csv-venv/bin/python -m pip install -r tools/requirements.txt
/tmp/c6-csv-venv/bin/python tools/csv_download.py --port /dev/ttyACM0 status
/tmp/c6-csv-venv/bin/python tools/csv_download.py --port /dev/ttyACM0 download lamparas.csv
```

Cierre antes `idf.py monitor` (`Ctrl+]`) y cualquier otro programa que use ese
puerto. Sustituya el puerto por el de su placa (`COM3`, por ejemplo, en Windows).
La herramienta no solicita un reinicio, pero en la placa probada abrir el USB
nativo puede reiniciar el C6. Reintenta `csv status` durante hasta 15 s y, para
comandos de repeticion, espera ademas a que Zigbee responda. No reintenta
automaticamente ordenes de inicio o borrado.
El firmware no necesita que haya un terminal conectado para seguir registrando.

La descarga verifica cantidad de bytes y CRC32 antes de publicar el archivo
final. Si falla, queda `lamparas.csv.part` para inspeccion y el comando devuelve
error; no sobrescribe archivos existentes. Se puede reintentar con otro nombre.
El CSV permanece en el C6 tras descargarlo. Para borrarlo y reanudar el registro:

```bash
/tmp/c6-csv-venv/bin/python tools/csv_download.py --port /dev/ttyACM0 clear --confirm
```

El borrado elimina los registros del archivo y los mensajes pendientes de la
cola. Las mediciones que lleguen despues se guardan en el archivo nuevo.
El borrado requiere almacenamiento montado; una particion que no monta no se
formatea desde estos comandos, para preservar su contenido.

Tambien se pueden enviar lineas desde un terminal serial, sin eco local:

```text
csv status
csv download
csv clear CONFIRM
```

`csv download` responde `@CSV BEGIN <bytes>`, seguido de exactamente esos bytes
del archivo y de `\n@CSV END <bytes> <crc32_hex>\n`. Las filas sincronizadas al
solicitar la descarga forman la instantanea; mensajes aun en cola y filas nuevas
quedan para la siguiente descarga. La recepcion y el guardado continuan durante
la transferencia. Los logs se descartan durante los comandos para no mezclarse
con el contenido. `Ctrl+C` cancela una descarga; desconectar el cable no borra
el archivo. Una descarga al limite tarda aproximadamente dos minutos a 115200.

### Verificacion

```bash
python3 -m unittest discover -s tools/tests -v
idf.py build
```

Las pruebas locales comprueban formato, valores negativos y flags, CRC32 y
recepcion fragmentada, errores de integridad e interrupciones de descarga.
En hardware hay que comprobar reinicio y corte de alimentacion, descarga con
telemetria simultanea, desconexion serial, llenado y borrado confirmado.

Validacion de banco anterior al cambio a USB nativo (2026-10-23): firmware cargado en ESP32-C6 revision v0.2,
flash de 8 MiB y puente CP2102N en `/dev/ttyUSB0`. Se verifico recepcion real del
H2 `c5feffb7`, incorporacion de una fila nueva, descarga con CRC32 y conservacion
exacta de cinco filas despues de reiniciar el MCU. Los contadores mostraron cero
errores de escritura y cero perdidas de cola. `csv clear` sin `CONFIRM` no borro
datos. Quedan pendientes las pruebas de llenado, borrado confirmado, corte durante
escritura y transferencia prolongada con recepcion simultanea.

## Compilar y grabar

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Si la placa aparece con otro nombre, reemplace `/dev/ttyACM0`. Para salir del
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

Para ejecutar una sola prueba de 30 segundos por el USB nativo, enviar
`zigbee test DEVICE_ID` con el identificador hexadecimal de ocho digitos.
El C6 exige una direccion no ambigua y vista en los ultimos 120 segundos.
`MANUAL_TEST sent` confirma el envio, no la ejecucion: comprobar
`self_test=1` en la telemetria del H2 y el reporte `TEST` al finalizar.
Este comando no programa repeticiones ni habilita las campanas automaticas.

Para repetir en una sola lampara hasta detenerla, cerrar el monitor serial y usar:

```bash
python3 tools/csv_download.py --port /dev/ttyACM0 repeat-start f96afdfc
python3 tools/csv_download.py --port /dev/ttyACM0 repeat-status
python3 tools/csv_download.py --port /dev/ttyACM0 repeat-stop
```

Tambien se puede enviar `zigbee repeat start f96afdfc`, `zigbee repeat status`
o `zigbee repeat stop` en el terminal serial. Solo se admite un nodo en esta
secuencia. Cada prueba dura 30 s y el reposo de 5 minutos empieza con la
confirmacion de AC y prueba inactiva. No se inicia sin telemetria reciente y
valida. Si no se confirma el inicio o el retorno a AC, se deshabilita la
repeticion en vez de reenviar. Parar cancela los ciclos futuros: una prueba
ya iniciada termina con el temporizador local de 30 s del H2.

El C6 guarda la seleccion y, despues de un reinicio, espera al menos 5 minutos
antes de reanudar. `download`
no borra el CSV ni cambia la seleccion. Las campanas globales permanecen aparte.

Para probar las dos lamparas de banco (`f96afdfc` y `8e6dcd6a`), cerrar el
monitor y detener cualquier repeticion del C6 antes de ejecutar:

```bash
python3 tools/lamp_test_cycle.py --port /dev/ttyACM0
```

El script mantiene una sola conexion USB, alterna las lamparas sin superponer
pruebas y espera al menos 5 minutos desde el retorno a AC de cada una. Si una
orden no se confirma, termina sin reenviarla. `Ctrl+C` impide nuevos ciclos;
el H2 termina cualquier prueba en curso con su temporizador local. Usar
`--cycles 2` para dos ciclos por lampara. No queda ninguna agenda en el C6 al
cerrar el script. Despues se puede descargar con `csv_download.py`.

**Ensayo cerrado el 2026-10-24:** `AUTO_TEST_ENABLED` esta desactivado en la
configuracion y los valores por defecto del proyecto. El C6 sigue recibiendo
y guardando telemetria, pero ya no programa descargas de bateria. El CSV se
conserva. Para reactivar el ensayo hay que habilitar `Enable automatic battery
test campaigns` en `idf.py menuconfig` y volver a cargar el firmware.

El C6 aprende la direccion corta y el `device_id` de cada H2 cuando recibe su
telemetria. Al comenzar una campana ordena los nodos por `device_id`, forma grupos
con un maximo exacto de 10 integrantes y envia automaticamente una agenda unicast
a cada lampara. No se requiere pulsar botones ni enviar ordenes individuales.

Perfil conservado para cuando se vuelvan a habilitar las campanas:

| Parametro | Valor |
|---|---:|
| Primer ciclo desde el arranque | 1 min |
| Intervalo entre inicios de ciclo | 60 min |
| Lamparas simultaneas | 10 |
| Duracion bajo bateria | 300 s (5 min) |
| Pausa entre grupos | 15 s |
| Preparacion del primer grupo | 10 s |

Perfil de descarga de bateria configurado el 2026-10-24: cada lampara funciona
5 minutos en bateria y recupera la senal de +7.5 V durante los 55 minutos restantes
antes de su siguiente prueba. Esto habilita la recarga; no garantiza que la bateria
alcance carga completa en ese intervalo. Se usa el maximo de 300 s admitido por el
H2 ya instalado; una prueba de 20 minutos requeriria actualizar tambien el H2.

Verificacion en placa del 2026-10-24: el H2 `c5feffb7` devolvio `PASS` con
`duration=300_s`, 61 muestras, tension minima de 6173 mV y corriente promedio
de 1857 mA. Se observaron `AC=0/self_test=1` durante la descarga y
`AC=1/self_test=0` al finalizar. El CSV nuevo se descargo con CRC correcto y
sin errores de escritura ni perdidas de cola. Los registros previos se borraron
por solicitud del usuario, despues de descargar una copia de respaldo.

La primera campana se prepara al minuto del arranque y la lampara comienza unos
10 s despues. Si aun no hay nodos o Zigbee no esta listo, se reintenta cada 5 s.
Los siguientes inicios se separan una hora, sin sumar el tiempo de envio de las
agendas. En cada grupo se mantiene el mismo retardo entre ciclos.

Sin Ethernet, RTC o NTP, el C6 usa tiempo relativo: reiniciarlo reinicia la
espera inicial de un minuto para las campanas globales.

Para una validacion de banco, cambie temporalmente `Minutes from gateway boot to
first campaign` a 1 desde:

```bash
idf.py menuconfig
```

Ruta: `Automatic battery tests`. Espere primero una linea `Registered node` para
confirmar que el C6 conoce la lampara. Durante una campana, la consola USB muestra la
asignacion, cada reporte y el resumen acumulado de aprobadas y fallidas.

Si el H2 habia pertenecido a otra red Zigbee, entre primero al proyecto
`../h2-emergency-light` y borre su flash antes de reintentar:

```bash
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

Ese comando borra tambien cualquier configuracion persistente del H2.

## Prototipo W5500 -> MQTT

En `idf.py menuconfig` -> `W5500 Ethernet and MQTT`, habilitar
`Enable W5500 Ethernet`. Esta opcion queda deshabilitada por defecto para
los gateways sin modulo. El pinout inicial coincide con el W5500 del esquema:

| W5500 | ESP32-C6 |
|---|---|
| SCK | GPIO6 |
| SI / MOSI | GPIO7 |
| SO / MISO | GPIO0 |
| CS | GPIO10 |
| INT | GPIO18 |
| RESET | GPIO19 |
| GND | GND comun |
| VCC | Segun especificacion de alimentacion del modulo W5500 |

SPI parte de 10 MHz (configurable), con reset externo y una MAC obtenida de la direccion Ethernet
reservada del C6. Los GPIO12/13 se reservan para USB. Usar cables SPI cortos y
una alimentacion estable, segun el modulo utilizado.

Con URI vacia se prueba solo Ethernet: conectar el RJ45 a una red con DHCP y
buscar `Ethernet link up` y `Ethernet DHCP IP=...` en la consola USB.
El W5500 negocia automaticamente velocidad y duplex con el switch (10/100 Mbps).
Se utiliza el controlador W5500 MAC RAW integrado en ESP-IDF 5.5, sin dependencia
externa del ENC28J60. RESET en GPIO19 lo gestiona el controlador PHY. La
alimentacion debe respetar la especificacion de la placa W5500 concreta.

Preferir el aprovisionamiento AT descrito abajo. Como alternativa de compatibilidad,
para incorporar valores de fabrica al binario, configurar `MQTT broker URI` y,
si corresponde, usuario y contrasena. Los valores se guardan en `sdkconfig`
local, ignorado por Git; no copiar credenciales a `sdkconfig.defaults`.
Para `mqtts://<host>:8883`, activar `Use MQTT TLS`, indicar la ruta absoluta
al certificado publico PEM de la CA/broker y configurar un servidor NTP
accesible. Se incrusta solo el certificado publico durante la compilacion.
El cliente espera sincronizacion NTP y valida cadena, fechas y nombre/IP del
certificado; no se desactiva la verificacion. El certificado debe cubrir el
host de la URI. `localhost` en el C6 apunta al C6, no al servidor del broker.
El servidor debe escuchar en una interfaz accesible desde la LAN.
MQTT sin TLS queda disponible solo para una red de banco confiable.

Alternativa antigua, importacion al binario (solo si no existe configuracion NVS):

```sh
python3 tools/configure_mqtt.py /ruta/gateway-config.json
idf.py build
idf.py -p /dev/ttyACM0 app-flash
```

El importador valida TLS, esquema, identidad, topics y tiempos. Guarda el PEM
publico en `mqtt-ca.local.pem` y las credenciales en `sdkconfig`, ambos ignorados
por Git. Conserva la seleccion local de pines y velocidad SPI. Este importador
antiguo requiere recompilar; `gateway_at.py` permite guardar por USB sin hacerlo.
La configuracion NVS tiene precedencia sobre esos valores compilados.
El certificado de Caddy/HTTPS de la web puede ser distinto del certificado de
Mosquitto: usar `mqtt.ca_pem` del JSON del gateway.

## Aprovisionamiento AT por USB, sin recompilar

Requiere este firmware y W5500 habilitado para conectar a la red. Usa el mismo
USB Serial/JTAG nativo (GPIO12/13) que CSV, a 115200, con lineas CRLF y sin eco.
El JSON exportado por la web puede contener metadatos como
`at_supported_by_current_firmware:false`: se toma su objeto `config`; esos
metadatos no son instrucciones ni cambian la capacidad real del firmware.

Desde el directorio del proyecto, cerrar antes cualquier monitor serial:

```sh
python tools/gateway_at.py --port /dev/ttyACM0 info
python tools/gateway_at.py --port /dev/ttyACM0 load /ruta/gateway-config.json --restart
python tools/gateway_at.py --port /dev/ttyACM0 --wait-mqtt 45 info
```

Sin `--restart` solo guarda. Aplicar despues con
`python tools/gateway_at.py --port /dev/ttyACM0 restart` o reiniciar la placa.
El comando `info` consulta datos no secretos y `mqtt_connected`; que se guarde
la configuracion no demuestra que el broker sea accesible o acepte credenciales.
El C6 sigue usando DHCP; cambiar `mqtt.host` cambia el servidor, no fija la IP del C6.
Abrir USB puede reiniciar algunas placas; el script espera la consola y
`--wait-mqtt 45` permite esperar DHCP/NTP/TLS manteniendo abierto el puerto.
Un estado inicial link/IP/MQTT en cero durante ese arranque no confirma un fallo.

| Comando | Respuesta / funcion |
|---|---|
| `AT` | `OK` |
| `ATI` | `+FFAT: firefly-at/v1,...` y `OK` |
| `AT+FFPROVBEGIN=N` | Inicia/reemplaza una transaccion RAM de N fragmentos |
| `AT+FFPROVDATA=i,"BASE64"` | Fragmento consecutivo, indice desde cero |
| `AT+FFPROVCOMMIT="SHA256"` | Valida y guarda; `+FFPROV: SAVED,RESTART_REQUIRED` y `OK` |
| `AT+FFPROV?` | Configuracion guardada, origen y `pending_restart`, sin clave ni PEM |
| `AT+FFPROVABORT` | Descarta transaccion sin modificar NVS |
| `AT+FFSTATUS?` | Enlace, IP DHCP, MQTT e identidad **activa** |
| `AT+RST` | `OK`, espera transmision y reinicia; no borra datos |

El formato es compatible con las lineas del contrato web `firefly-at/v1-draft`;
el receptor se identifica como `firefly-at/v1`. Al pegar un TXT de la web,
transmitir solo lineas AT, no comentarios. Base64 transporta bytes UTF-8 del
JSON y no cifra las credenciales. El script no muestra el payload ni la clave.

Limites: JSON 8192 bytes, CA PEM 4096 bytes, hasta 3 servidores NTP,
fragmentos base64 de 192 caracteres (el ultimo puede ser menor), hasta 57
fragmentos y lineas de hasta 255 caracteres. Cada fragmento no final debe tener
192 caracteres. La transaccion expira tras 60 s sin fragmentos validos; se
descarta tambien al abortar, desconectar/reiniciar, fallar validacion o recibir
una linea demasiado larga. Repetir el ultimo fragmento identico devuelve `OK`
sin duplicarlo. No hay aplicacion parcial.

`COMMIT` verifica orden, cantidad, base64, SHA-256 y contrato completo: Ethernet
DHCP; TLS y verificacion obligatorios; URI/host/puerto coherentes; certificado
X.509 parseable; identidad, usuario, topics, QoS1, MQTT3.1.1 y tiempos. El
nombre/IP del **servidor** y las fechas se verifican al conectar despues de NTP.
No se desactiva TLS si falla. Los pines y velocidad SPI siguen siendo opciones
de compilacion porque dependen de la placa.

Se guarda un blob versionado `gateway_prov/config`, con SHA-256 e integridad
verificada al arrancar, mediante `nvs_set_blob` + `nvs_commit`. La misma carga
exacta no reescribe NVS. Una carga invalida, incompleta o con error de escritura
conserva la anterior. Datos corruptos se preservan: no se vuelve silenciosamente
a otras credenciales; la consola AT permite reemplazarlos explicitamente.
Cambios guardados se aplican juntos al reiniciar, manteniendo estables los
punteros y topics de MQTT mientras se ejecuta la conexion actual. No se borran
la red Zigbee, el CSV ni los resultados/deduplicacion de pruebas.

Los parametros consultados con `FFPROV?` son los guardados para el proximo
arranque si `pending_restart=1`; `FFSTATUS?` describe la conexion en ejecucion.
Errores AT usan codigos como `ORDER`, `TIMEOUT`, `INCOMPLETE`, `BASE64`,
`CHECKSUM`, `CONFIG`, `STORAGE` y `LINE_TOO_LONG`, nunca el texto recibido.
La NVS actual no usa cifrado de flash: las credenciales quedan en la flash del
dispositivo. Mantener privados tanto el JSON exportado como el acceso USB.

Pruebas de software:

```sh
python -m unittest discover -s tools/tests -v
```

Las pruebas AT ejecutan el parser/almacenamiento C real con ASan/UBSan,
SHA-256/base64/X.509 reales mediante un puerto OpenSSL y NVS/tiempo simulados.
Comprueban repeticion, orden, timeout, validacion, fallos set/commit, reinicio,
corrupcion, redaccion de secretos y compatibilidad del generador de fragmentos.

Validacion fisica del **2026-11-18**:

- Se compilo con ESP-IDF 5.5 y W5500 habilitado, sin claves MQTT incorporadas
  al binario. Tambien compilo la variante USB sin Ethernet en build aislado.
- Se cargo el firmware por `/dev/ttyACM0` y el archivo de configuracion nuevo
  mediante `gateway_at.py load ... --restart`, sin recompilar sus parametros.
- Tras reiniciar y volver a flashear el firmware, `FFPROV?` recupero `source=nvs`,
  `pending_restart=0`, identidad `gateway-standar`, URI
  `mqtts://192.168.18.40:8883`, client ID `firefly-gateway-standar`, TLS activado,
  un NTP, keepalive/heartbeat de 60 s y clave presente, sin revelarla.
- `FFSTATUS?` confirmo enlace Ethernet, IP DHCP **192.168.18.111** y
  **mqtt_connected=1**. Un observador autenticado en el broker nuevo recibio
  telemetria real de `f96afdfc` en su topic, con revision de bateria 3.
- Una transaccion con SHA-256 deliberadamente incorrecto devolvio
  `ERROR:CHECKSUM`; la configuracion NVS anterior se conservo. Las consultas
  no devolvieron la contrasena ni el PEM.
- Respaldo CSV previo: `/tmp/c6-before-at-20261118.csv`, **402167 bytes**,
  CRC32 **b4536a1b**. Se mantuvieron red Zigbee y datos; no se ejecuto borrado.
- Las **22 pruebas host** del C6 pasaron, incluyendo regresion de CSV,
  configuracion de bateria y pruebas individuales. No se modifico el H2.

- Raiz: `firefly/v1/gateways/<gateway_id>`; el usuario es el gateway registrado.
- `/status`: JSON v1 retenido, `online`, `boot_id` y `message_id`. Heartbeat
  configurable (60 s por defecto), y ultima voluntad `online:false`. Cada
  conexion utiliza un ID distinto para su ultima voluntad.
- `/telemetry`: JSON v1 segun el contrato Firefly, incluyendo `voltage_mv`,
  `current_ma`, `power_mw`, `temperature_centi_c`, `soh_pct`, `sequence`,
  `flags`, `uptime_s`, `report_interval_s`, `live` y capacidad real por nodo.
- Se omite `measured_at`: Zigbee no incluye fecha de medicion. La web usa
  recepcion. Para v1, el `boot_id` identifica un flujo heredado observado
  por el gateway: cambia al reiniciar el C6 o detectar retroceso de uptime;
  no constituye un identificador nativo de arranque del H2 y tiene limites
  ante paquetes fuera de orden. Para v2 se propaga el boot ID nativo del H2.
- Publicacion QoS 1 sin retencion para telemetria; `message_id` permite
  deduplicar reintentos. Solo los H2 v2 compatibles anuncian `config_capable:true`.
- Una cola de 16 muestras separa Zigbee de MQTT; el outbox se limita a 32 KiB.
  El importador establece expiracion de mensajes pendientes a 15 s. Solo se
  aceptan muestras para MQTT con conexion activa; las que esperan mas de 5 s
  en la cola se marcan `live:false`. Al llenarse se descartan con aviso.
  No hay replay automatico desde CSV; los pendientes se pierden al reiniciar.
- CSV conserva su flujo y limites propios. Ethernet no borra NVS, red ni CSV.

Verificar link, DHCP y `MQTT connected`, recepcion de una lampara y recuperacion
al desconectar/reconectar Ethernet. La compilacion no verifica estas pruebas
fisicas. El gateway tambien recibe comandos de configuracion y prueba individual,
y publica ACK de configuracion y reportes completos de bateria.

## Prueba individual desde FireFly Web

El nuevo módulo `remote_test.c` recibe `commands/test` por MQTT QoS1, sin retain, para una lámpara identificada por ocho dígitos hexadecimales. La prueba dura 30 s y usa el comando Zigbee de programación existente con demora cero. El C6 publica `test-ack` y reenvía los reportes H2 por `test-report`, vinculando `command_id` cuando coincide el nodo y `test_id`. Las campañas automáticas reservan IDs 1–32767; la web usa 32768–65535. No se modifica el protocolo binario H2.

El broker debe permitir al usuario gateway leer su topic `commands/test` y escribir su topic `test-ack`. La web incorpora esas ACL. El C6 anuncia `test_capable:true` con reloj sincronizado; sin esa capacidad la ficha muestra el botón deshabilitado. La sincronización NTP del transporte TLS existente es necesaria para validar el vencimiento.

Se verifica UUID, destino, duración, antigüedad y vencimiento. El identificador y marca temporal se registran en NVS antes de enviar a Zigbee: duplicados y reinicios no reejecutan la orden. Si el envío físico quedó incierto, se informa `unknown`. Una orden ya transferida al H2 puede persistir allí y recuperarse tras su reinicio según el comportamiento actual de `battery_test`; el vencimiento MQTT no la cancela. No reenviar ciegamente una prueba sin resultado.

El C6 evita solapar estas solicitudes con campañas automáticas o ciclos repetitivos; una solicitud puede rechazarse por ocupación. La reserva se libera al recibir el reporte o después de tres minutos. El H2 conserva sus validaciones y su límite local de activación del relé.

Verificación sin placa:

```sh
source ~/esp/esp-idf/export.sh
idf.py build
python3 tools/tests/test_remote_test.py
```

La prueba de host compila el parser C real con cJSON de ESP-IDF y puertos simulados de NVS/MQTT/RTOS. Comprueba persistencia antes de actuar, duplicados, vencimiento, rechazo, corte entre envío/commit, recuperación y fragmentación MQTT. No demuestra comportamiento eléctrico. El ensayo físico de FW-001 del 2026-11-17 sí cargó C6/H2 y confirmó orden web, estado de prueba, retorno a AC y reporte con revisión original; su resultado eléctrico fue LOW_CURRENT. Consultar la evidencia y límites al final de este documento.

## Configuracion de bateria por lampara (FW-001)

Con Ethernet/MQTT habilitado, el C6 se suscribe a
`firefly/v1/gateways/<gateway_id>/commands/config`, QoS1, sin retain. Recibe
el contrato actual de la web: `schema_version:1`, `command_id` UUID,
`device_id` hex8, `version` uint32 positivo por lampara y `thresholds` con
los siete uint16 en mV. No exige quimica ni voltaje nominal en MQTT.

Valida JSON y ensambla fragmentos contiguos de hasta 1024 bytes. La cola admite
ocho solicitudes. Resuelve una direccion reciente (120 s), sin colision con
otro ID, y exige capacidad anunciada en telemetria v2. Un nodo v1 sigue
recibiendose con capacidad falsa. No se envia el perfil a un nodo desconocido
ni se inventa un ACK ante fallo de envio: la web puede reintentar configuraciones.

El H2 recibe Zigbee `0x05`; su ACK `0x06` debe coincidir con ID y direccion
registrados. El C6 publica `/config-ack` solo a partir de esa respuesta:
`applied`, `deferred`, `rejected`, motivo y UUID/revision originales.
Motivos: `invalid`, `stale`, `conflict`, `storage`; `none` indica exito.
El PUBACK MQTT o el envio Zigbee no significan aplicacion.

Hay cache RAM FIFO de 16 respuestas distintas, reenvio al reconectar y
reintento de encolado cada segundo. La cache no sobrevive al reinicio del C6;
la web reintenta contra el H2 idempotente y el H2 reanuncia cada 30 s su estado
persistido. Los reenvios de la misma respuesta mantienen `message_id`.

Cada muestra v2 conserva su `config_version` activa y cada reporte la revision
del inicio de la prueba. Revision cero/heredada se omite en MQTT; no equivale
a un perfil web aplicado. Se conserva revision y boot ID en el resultado
remoto persistido del C6; registros antiguos de deduplicacion se migran sin
repetir la prueba ni inventar revision. CSV/USB mantienen sus columnas y descarga
actuales: el CSV no incorpora la revision de perfil; MQTT si la transporta.

Actualizar C6 y H2 mediante `idf.py -p PUERTO flash`, conservando NVS,
`zb_storage` y CSV. No se necesita borrar la red para esta actualizacion.
Los H2 v2 requieren un C6 nuevo. No se introducen perfiles reales de 4/6/12 V
por defecto: configurarlos segun la bateria y las protecciones del controlador.

```sh
python -m unittest discover -s tools/tests -p 'test_remote*.py' -v
```

El parser se prueba con ASan/UBSan, fragmentacion, JSON invalido, reconexion,
ACK y contrato Zod real de la web cuando estan instaladas sus dependencias.
La descarga USB bloquea un tick entre bloques para permitir ejecutar IDLE:
un simple `taskYIELD()` puede activar el watchdog durante archivos largos e
intercalar su diagnostico en la transferencia; el descargador lo rechaza por CRC.
La evidencia de compilaciones/carga/pruebas fisicas esta en
`../../firefly-web/plan/firmware/FW-001-configuracion-bateria-h2-c6.md`.
