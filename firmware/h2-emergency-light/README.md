# ESP32-H2 - Nodo de luminaria

Firmware ESP-IDF para medir tension, corriente, potencia y temperatura de una
luminaria, controlar su prueba de bateria y enviar telemetria al C6. El H2
funciona como router Zigbee 3.0. Los umbrales actuales son fijos para una bateria
de 6 V; todavia no se pueden cambiar remotamente por lampara.

## Estructura del codigo

| Modulo | Responsabilidad |
|---|---|
| `main.c` | Inicializacion y arranque de modulos |
| `app_config.h` | GPIO, intervalos, umbrales y constantes de hardware |
| `ina226.c/.h` | Bus I2C, deteccion y medidas de tension, corriente y potencia |
| `ntc.c/.h` | ADC, calibracion y conversion del termistor |
| `relay.c/.h` | Entrada AC, rele fail-safe y boton QA |
| `battery_test.c/.h` | Agenda, ejecucion, NVS, estadisticas y reporte de pruebas |
| `telemetry.c/.h` | Muestreo, SoH, alertas e intervalo adaptativo |
| `zigbee_node.c/.h` | Router, asociacion, comandos y transmision Zigbee |

El protocolo compartido esta en `../common/include/emergency_protocol.h`.

## Pinout

| Funcion | GPIO ESP32-H2 | Conexion |
|---|---:|---|
| I2C SDA | GPIO4 | INA226 SDA |
| I2C SCL | GPIO12 | INA226 SCL |
| INA226 ALERT | GPIO11 | ALERT, activo bajo; pull-up a 3.3 V |
| NTC ADC | GPIO1 / ADC1_CH0 | Punto medio del divisor NTC |
| AC presente | GPIO10 | Salida logica 0/3.3 V de U5 despues de R9/R10 |
| Control de rele | GPIO2 | Entrada del driver de bobina; el firmware asume activo alto |
| Boton TEST | GPIO5 | Pulsador a GND, pull-up interno |
| USB D- | GPIO26 | Programacion y monitor USB-JTAG |
| USB D+ | GPIO27 | Programacion y monitor USB-JTAG |

El divisor NTC esperado es `3.3 V -> 10K fija -> GPIO1 -> NTC 10K B3950 -> GND`.
GPIO10 **no puede conectarse directamente a 220 V ni a 12 V**. En la PCB recibe
la salida acondicionada de U5 mediante el divisor R9/R10: J4.2/K1.12 llevan la
senal de control de `+7.5 V`, U5 la convierte a un nivel logico de 0/3.3 V y su
salida llega a GPIO10. El firmware habilita pull-down para que una entrada
desconectada se interprete como AC ausente.

## Rele de prueba de bateria

El contacto **normalmente cerrado (NC)** del rele conmuta la senal de control de
`+7.5 V` que llega al controlador de la lampara por J4.2; no conmuta la red de
220 V. En reposo la bobina esta apagada y la senal de AC esta presente. Al
pulsar `TEST`, GPIO2 energiza la bobina durante 30 segundos y abre el contacto,
haciendo que el controlador de la lampara simule la perdida de AC y pase a
bateria. Una segunda pulsacion cancela la prueba. El firmware limita cualquier
prueba a un maximo absoluto de 300 segundos. El inicio y el final despiertan
inmediatamente la tarea de telemetria para que ambos estados sean reportados sin
esperar el intervalo normal de 60 segundos.

### Tabla de verdad

`AC presente` funciona como permiso de seguridad para iniciar la prueba; no
controla directamente el rele. `Prueba activa` es el estado interno que puede
iniciarse mediante el boton QA o una agenda Zigbee.

| AC presente GPIO10 | Prueba activa | GPIO2 | Bobina activa-alto | Contacto NC | Alimentacion esperada |
|---:|---:|---:|---|---|---|
| 0 | 0 | 0 | Apagada | Cerrado | Controlador en bateria / sin senal AC |
| 1 | 0 | 0 | Apagada | Cerrado | Controlador en modo AC |
| 0 | 1 | 1 | Encendida hasta timeout | Abierto | Bateria |
| 1 | 1 | 1 | Encendida hasta timeout | Abierto | Bateria de forma intencional |

El firmware no inicia una prueba nueva cuando GPIO10 indica que AC esta ausente.
Sin embargo, si AC desaparece despues de comenzar una prueba, el timeout local
mantiene el control y finalmente devuelve GPIO2 a cero.

| Evento del boton GPIO5 | AC presente | Estado anterior | Resultado |
|---|---:|---|---|
| Sin pulsar | Cualquiera | Reposo | Sin cambios, GPIO2 permanece en 0 |
| Pulsacion | 1 | Reposo | Inicia prueba de 30 s, GPIO2 pasa a 1 |
| Pulsacion | 0 | Reposo | Prueba rechazada, GPIO2 permanece en 0 |
| Pulsacion | Cualquiera | Prueba activa | Cancela prueba y GPIO2 vuelve a 0 |

### Polaridad del modulo de rele

La tabla anterior supone un driver **activo-alto**. Algunos modulos comerciales
son activos-bajo y producen el comportamiento inverso:

| GPIO2 | Modulo activo-alto | Modulo activo-bajo |
|---:|---|---|
| 0 | Bobina apagada | Bobina encendida |
| 1 | Bobina encendida | Bobina apagada |

Si el rele permanece encendido desde el arranque, mida GPIO2 respecto de GND:

- En reposo debe medir aproximadamente 0 V.
- Durante una prueba debe medir aproximadamente 3.3 V.
- Si esos niveles son correctos pero la bobina funciona al contrario, el modulo
  es activo-bajo y debe invertirse la polaridad en el firmware o en el driver.
- Si GPIO2 permanece alto mas alla de la duracion solicitada, revise si hay
  otra prueba o una agenda restaurada y compruebe el driver del rele.

La polaridad se configura en `main/app_config.h`:

```c
#define APP_RELAY_ACTIVE_LEVEL 1  // Driver activo-alto
```

Use `0` si el modulo se activa al recibir nivel bajo. Al iniciar y al cambiar de
estado, el firmware imprime tanto el nivel solicitado como el leido en el pad:

```text
relay: Relay initialized: GPIO2 active_level=1 requested_level=0 readback=0
relay: Relay test_active=1 requested_level=1 readback=1
```

Si `requested_level` y `readback` cambian juntos, el GPIO funciona y la causa
esta en la polaridad, GND comun, alimentacion o etapa driver. Si el `readback` no
sigue al nivel solicitado, desconecte temporalmente la entrada del modulo y mida
GPIO2 sin carga para detectar un corto o una carga externa excesiva.

### Pruebas locales y remotas

- **Boton TEST:** inicia una prueba de 30 s con AC presente. Otra pulsacion la cancela.
- **Orden inmediata Zigbee `0x02`:** acepta 1–300 s; con duracion cero cancela
  la prueba activa. El C6 usa `zigbee test DEVICE_ID` para pedir 30 s a un solo H2.
- **Agenda Zigbee `0x03`:** el H2 guarda en NVS la prueba y su retardo. El ciclo
  de 30 s en bateria y 5 min en AC de las herramientas del C6 se coordina fuera
  del H2; el nodo ejecuta cada orden y no programa esa repeticion por su cuenta.

El temporizador local devuelve el rele a NC al finalizar, aunque se pierda
Zigbee. La telemetria se despierta al comenzar y al terminar. `self_test=0`
indica que la prueba termino; `AC=1` confirma que GPIO10 volvio a detectar la
senal de AC. Pueden aparecer en mensajes distintos mientras cambia la entrada.

El reporte Zigbee `0x04` incluye duracion solicitada, numero de muestras,
tension previa y minima, corriente promedio y pico, potencia promedio,
temperatura maxima y resultado. Si el envio falla, el H2 conserva un reporte
pendiente en NVS y reintenta cuando Zigbee esta disponible. Un reporte perdido
en el C6 no demuestra que el rele siga activo.

Para el resultado de la prueba se usan los limites fijos de **5,25 V** de
tension minima, **50 mA** de pico de magnitud de corriente y **40 °C** de
temperatura maxima. `LOW_VOLTAGE` en una bateria de 4 V puede deberse a ese
perfil de 6 V. Los umbrales de tension y temperatura **no cortan por si solos**
una prueba activa; se aplican al evaluar sus mediciones.

GPIO2 no puede alimentar una bobina. La PCB usa un MOSFET y diodo flyback para
accionar la bobina. En esta revision:

- La bobina se alimenta desde el rail previsto y la controla el MOSFET de Q1.
- J4.2/K1.12 son una interfaz de baja tension DC; no deben conectarse a la red.
- Si el producto debe conmutar 220 VAC, se necesita una etapa de potencia
  separada, certificada y revisada segun la normativa aplicable.

El H2, el USB y los sensores no deben compartir una conexion electrica directa
con la red AC.

Se evitan GPIO8, GPIO9 y GPIO25 porque son strapping pins; GPIO15 a GPIO21 se
reservan para la flash interna del modulo. GPIO13 y GPIO14 tampoco se usan porque
pueden estar conectados al cristal de 32.768 kHz dentro del ESP32-H2-MINI-1.

## INA226

- Direccion I2C: `0x40`.
- Shunt de la PCB: `R5 = 2 mOhm`.
- Current LSB: 100 uA.
- Registro de calibracion: 25600 (`0x6400`, `0.00512 / (100 uA * 2 mOhm)`).
- La corriente se transmite con el signo leido por el INA226. La convencion
  prevista es positivo para descarga, pero el signo real depende de la
  orientacion del shunt. Verificarlo con carga y recarga conocidas antes de
  interpretar el CSV; el firmware no corrige automaticamente la polaridad.

Si el INA226 no responde durante el arranque, el firmware registra una advertencia
pero mantiene activos Zigbee y el control fail-safe del rele. Las medidas se
publican con `sensor_valid=false`, sin generar una falsa alerta UVLO por valores
en cero. El arranque escanea todo el bus y acepta automaticamente las direcciones
INA226 `0x40` a `0x4F`. Revise alimentacion, GND, SDA y SCL si no aparece ninguna.

El INA226 no limita electricamente la corriente. A 1.06 A, el shunt `R5`
solo debe producir unos 2.12 mV de caida y disipar unos 2.25 mW. Si al insertar
el modulo la corriente cae de forma apreciable, mida con la carga encendida:

1. Tension directamente entre `IN+` e `IN-`: debe ser cercana a 2.12 mV a
   1.06 A.
2. Tension entre bateria positiva y entrada positiva de la lampara, incluyendo
   modulo, cables y conectores: una caida mucho mayor identifica una conexion
   resistiva o un recorrido incorrecto.
3. Corriente con multimetro e INA226 simultaneamente en serie: comparar pruebas
   hechas sustituyendo el modulo no permite separar un error de calibracion de
   una limitacion fisica.

La corriente de la lampara debe circular exclusivamente por los terminales de
potencia `IN+` e `IN-`, con cables cortos y adecuados para mas de 1 A. No debe
circular por `VCC`, GND ni por cables Dupont de senal.

## Zigbee

El `device_id` se calcula con CRC32 sobre los ocho bytes de la direccion Zigbee
EUI-64 y se muestra al arrancar. Al actualizar desde la version anterior, cambia
el identificador en los nuevos registros; los CSV historicos no se modifican.
El formato Zigbee sigue usando 32 bits, por lo que no se necesita actualizar el C6.

El dispositivo funciona como router Zigbee (`ZCZR`), usa canal 15 y envia la
telemetria descrita en `../PROTOCOL.md` al coordinador `0x0000`. Mantiene la radio
disponible para reenviar tramas mientras tenga alimentacion, incluso durante
una prueba si el H2 no se apaga. El C6 es el unico coordinador.

Para migrar un H2 ya emparejado como End Device, borrar solo su particion Zigbee
`zb_storage` antes de grabar el firmware router. No usar `erase-flash` ni borrar
la red del C6. Con ESP-IDF activado, desde el proyecto H2:

```bash
python "$IDF_PATH/components/partition_table/parttool.py" --port /dev/ttyACM0 erase_partition --partition-name zb_storage
idf.py -p /dev/ttyACM0 flash monitor
```

Identificar el puerto de cada H2 y cerrar su monitor antes de borrar o flashear.
Emparejar cerca del C6 dentro de su ventana de incorporacion y confirmar los IDs
de ambas lamparas. Para verificar un salto, colocar B fuera del alcance directo
del C6 pero al alcance de A, y comprobar que la telemetria de B se pierde al
apagar A y se recupera al encenderla. Repetir con A ejecutando su prueba local
para confirmar que la conmutacion no interrumpe el reenvio.

## Telemetria y umbrales actuales

El H2 mide aunque Zigbee no este asociado; envia las muestras al C6 cuando
esta unido. Las condiciones se evaluan en este orden:

| Condicion | Intervalo |
|---|---:|
| Prueba activa | 5 s |
| Alerta: tension valida por debajo de 5,50 V o temperatura valida de al menos 40 °C | 10 s |
| Sin alerta, tension reportada por debajo de 5,85 V (incluye cero si falla el INA226) | 30 s |
| Resto | 60 s |

`UVLO` indica una medicion valida por debajo de 5,25 V. `sensor_valid` requiere
lecturas validas tanto del INA226 como del NTC. El valor `soh_pct` se estima
solo a partir de la tension y del estado AC; no mide la capacidad ni el estado
de salud real de la bateria. En una lampara de 4 V, el perfil fijo puede producir
`alert`, `UVLO`, `soh_pct=0` y reportes cada 10 s sin que eso diagnostique la bateria.

## Compilar con ESP-IDF 5.5

```bash
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Si es una configuracion nueva, ejecutar `idf.py set-target esp32h2` antes de
compilar. Si existe un `sdkconfig` de una version antigua, seleccionar el stack
coordinador/router (`CONFIG_ZB_ZCZR=y`) en `idf.py menuconfig`. El firmware ya
no se compila en modo End Device (`ZED`).
