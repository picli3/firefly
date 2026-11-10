# ESP32-C6 - Gateway Zigbee por USB nativo

Forma la red Zigbee de la luminaria, recibe su telemetria y la guarda en un CSV
persistente en la flash interna. Permite descargarlo por USB Serial/JTAG y
mantiene los mensajes legibles de diagnostico. No usa Wi-Fi ni MQTT.

## Estructura del codigo

| Modulo | Responsabilidad |
|---|---|
| `main.c` | Inicializacion de NVS y arranque de modulos |
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
- Payload `emergency_telemetry_t` version 1 descrito en `../PROTOCOL.md`.
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
