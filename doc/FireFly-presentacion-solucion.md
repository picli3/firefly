> [!portada]
> ![[logo-firefly.png]]
>
> # FireFly
> ## Supervisión inteligente de luces de emergencia
>
> ---
>
> **Maykol Rey**
> 2026
> Versión 1.0

> [!indice] Índice
> ```table-of-contents
> style: nestedList
> minLevel: 2
> maxLevel: 3
> includeLinks: true
> ```

**Saber hoy, y no durante el próximo corte de luz, si cada luminaria de emergencia va a responder.**

FireFly convierte las luminarias de emergencia existentes en equipos conectados. Mide la batería de cada luminaria, programa pruebas automáticas y reúne toda la información en una aplicación web privada, con reportes listos para presentar. El responsable de mantenimiento deja de recorrer el edificio pulsando botones de prueba y pasa a ver desde su computadora o su teléfono qué luminarias están bien, cuáles necesitan atención y cuándo se probó cada una por última vez.

---

## 1. El problema: luces que tienen corriente pero no responden

Una luz de emergencia puede estar enchufada, con su piloto encendido, y aun así quedarse a oscuras a los pocos minutos de un corte. La batería se degrada con el tiempo y el calor, y ese desgaste no se ve desde fuera.

El método tradicional para detectarlo es manual:

- Un técnico recorre el edificio, luminaria por luminaria, y pulsa el botón de prueba.
- Observa si enciende y anota el resultado en una planilla o un cuaderno.
- Repite el recorrido cada mes, con el costo de horas de trabajo y de acceso a zonas altas u ocupadas.
- El resultado es un «sí, encendió» puntual. No queda registro de cómo se comportó la batería ni de si viene empeorando.

En la práctica, muchas luminarias fallan entre una revisión y la siguiente, y nadie se entera hasta que se necesitan.

## 2. La solución FireFly

![Arquitectura de la solución FireFly: luminarias en red inalámbrica, gateway por zona, servidor y aplicación web](img/presentacion/arquitectura-solucion.png)

*Figura 1. De la luminaria al navegador: cada luminaria con su módulo FireFly, un gateway por zona y el servidor con la aplicación web en la red del propio cliente.*

La solución tiene cuatro piezas:

| Pieza                | Qué es                                                                  | Qué hace                                                                                                                                                                                        |
| -------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Módulo FireFly**   | Una pequeña placa que se instala dentro de cada luminaria de emergencia | Mide continuamente la batería (tensión, corriente y temperatura) y puede ejecutar una prueba real: simula el corte de luz, hace funcionar la luminaria con su batería y registra cómo responde. |
| **Gateway FireFly**  | Un equipo por zona o planta, conectado a la red del edificio            | Coordina la red inalámbrica de las luminarias, recibe sus datos y los envía al servidor. Lanza las pruebas que se ordenan desde la aplicación.                                                  |
| **Servidor FireFly** | Un equipo compacto instalado en la red del cliente                      | Guarda el histórico de mediciones y pruebas, ejecuta las pruebas programadas y genera los reportes. No depende de servicios en la nube.                                                         |
| **Aplicación web**   | Se abre desde cualquier navegador (PC, tablet o teléfono)               | Vista general del estado de todas las luminarias, ficha detallada de cada una, planos del edificio, planificación de pruebas y reportes.                                                        |

![Prototipo del módulo FireFly que se instala dentro de la luminaria](img/photo_2026-11-16_12-33-53.jpg)
*Figura 2. Prototipo del módulo FireFly. Se aloja dentro de la luminaria y aprovecha su propia batería, cargador y luces: no se reemplaza el equipo existente.*

## 3. Cómo se comunican las luminarias

<p align="center"><img src="img/arquitectura_general.png" alt="Red en malla: cada luminaria retransmite los mensajes de sus vecinas hasta llegar al gateway" width="420"></p>

*Figura 3. Las luminarias forman una red en malla. Un mensaje puede saltar de luminaria en luminaria hasta alcanzar el gateway, que lo envía por cable de red.*

- **Red inalámbrica propia (Zigbee).** Las luminarias no usan el Wi-Fi del edificio ni necesitan claves de esa red. Zigbee es un estándar de bajo consumo pensado para sensores y automatización de edificios.
- **Red en malla.** Cada luminaria energizada actúa también como repetidor. Si una luminaria está lejos del gateway, sus mensajes viajan a través de las vecinas. Al agregar luminarias, la cobertura crece en lugar de saturarse.
- **Un solo cable para el gateway.** El gateway se conecta a la red del edificio con un cable Ethernet que también le da energía (PoE). No necesita un enchufe aparte.
- **Comunicación cifrada y con credenciales propias.** Cada gateway tiene su propio usuario y contraseña, y la conexión con el servidor va cifrada (TLS). Un gateway solo puede enviar sus propios datos y recibir sus propias órdenes.
- **Informa con más frecuencia cuando hace falta.** En condiciones normales cada luminaria reporta cada minuto; si la tensión baja o aparece una alerta, reporta cada 30 o cada 10 segundos.
- **Privado por diseño.** Los datos se quedan en el servidor del cliente. El acceso a la aplicación exige usuario y contraseña, y hay dos perfiles: **administrador**, que configura, y **monitor**, que consulta y descarga reportes.

## 4. Ventajas frente al método tradicional

| | Método tradicional | Con FireFly |
| --- | --- | --- |
| **Cómo se prueba** | Un técnico pulsa el botón de cada luminaria | Desde la aplicación, una luminaria a la vez o de forma programada |
| **Frecuencia** | Mensual o menos, según disponibilidad de personal | Los días y a la hora que se elijan, sin desplazamientos |
| **Qué se sabe de la batería** | Si encendió o no en ese momento | Tensión, corriente y temperatura durante toda la prueba, con historial para ver la tendencia |
| **Detección de fallas** | En la próxima revisión, o durante una emergencia real | Alertas y estado en vivo: luminaria sin comunicación, en batería, con tensión baja, sensor fuera de rango |
| **Evidencia** | Planillas en papel o en hojas de cálculo | Registro automático de cada prueba, con fecha, duración y resultado, y reportes PDF y CSV |
| **Ubicación** | Listados o memoria del personal | Cada luminaria y cada gateway posicionados en el plano del edificio |
| **Costo operativo** | Horas de recorrido, escaleras y coordinación con los ocupantes | Revisión remota; el técnico va solo a las luminarias que lo necesitan |

FireFly no reemplaza las inspecciones que exija la normativa aplicable. Las complementa con datos continuos y orienta el mantenimiento hacia las luminarias que muestran señales de deterioro.

## 5. Cómo se usa la aplicación web

### Vista general

![Vista general con indicadores, filtros y la lista de luminarias](img/presentacion/01-vista-general.png)

*Figura 4. Vista general de una instalación piloto con dos luminarias en línea.*

La pantalla de inicio responde de un vistazo a «¿cómo está la instalación?»:

- **Indicadores:** luminarias registradas, en línea, con alertas y sin comunicación. Al pulsar uno, la lista se filtra por ese estado.
- **Filtros** por sede, edificio, nivel, gateway y estado, más un buscador por nombre o lugar.
- **Lista de luminarias** ordenada para mostrar primero lo que requiere atención. Cada fila indica estado, batería, temperatura y última comunicación.
- **Pruebas programadas:** el icono de calendario junto al nombre indica que la luminaria tiene pruebas planificadas. Al pasar el cursor muestra cuándo será la próxima.
- Se actualiza en vivo, sin recargar la página.

### Ficha de cada luminaria

![Ficha de una luminaria con sus indicadores y gráficos de los últimos 7 días](img/presentacion/02-ficha-lampara.png)

*Figura 5. Ficha de una luminaria: valores actuales y evolución de tensión, corriente, temperatura y potencia en los últimos 7 días.*

Cada luminaria tiene su ficha, organizada en pestañas:

- **Resumen:** valores actuales y gráficos con periodos de 1 hora, 24 horas, 7 días, 28 días, 3 meses y 1 año. Al pasar el cursor sobre la curva se ve el valor exacto en cada momento. Las caídas de tensión que se ven en la figura corresponden a las pruebas de batería.
- **Pruebas:** botón para lanzar una prueba al momento, con confirmación previa, e historial de solicitudes y resultados.
- **Planificación:** pruebas automáticas recurrentes (ver más abajo).
- **Configuración:** nombre, ubicación y perfil de batería de la luminaria.

![Historial de pruebas de una luminaria con sus resultados](img/presentacion/04-pruebas.png)

*Figura 6. Pestaña Pruebas: cada solicitud queda registrada con su estado y su resultado.*

### Planificación de pruebas

![Pestaña de planificación con días de la semana, hora y duración](img/presentacion/03-planificacion.png)

*Figura 7. Una luminaria programada todos los días a las 20:30 con pruebas de 2 minutos.*

Para cada luminaria se eligen los **días de la semana**, la **hora** y la **duración** de la prueba, de 30 segundos a 5 minutos. A partir de ahí, el sistema la ejecuta solo:

- A la hora indicada, el servidor ordena la prueba y la luminaria pasa a funcionar con su batería durante el tiempo elegido.
- Si en ese momento la luminaria no puede probarse, por ejemplo porque no hay energía o hay otra prueba en curso, el sistema reintenta durante 15 minutos. Si no lo consigue, la registra como **omitida** e indica el motivo.
- Cada ejecución queda en el historial con su resultado. La planificación se puede pausar sin borrarla.

### Planos del edificio

![Plano del edificio con las luminarias y el gateway ubicados](img/presentacion/05-planos.png)

*Figura 8. Plano con las luminarias y el gateway. El color indica si cada equipo se está comunicando.*

Se carga el plano de cada nivel (archivo CAD en formato DXF) y se ubica cada luminaria y cada gateway con un clic. El plano muestra en vivo qué equipos están activos y permite abrir la ficha de cualquier luminaria desde su punto.

### Desde el teléfono, de día o de noche

<p align="center"><img src="img/presentacion/09-movil-oscuro.png" alt="Vista general en un teléfono con el modo oscuro" width="300"></p>

*Figura 9. La aplicación se adapta al teléfono e incluye modo oscuro.*

## 6. Instalación y configuración del gateway

La puesta en marcha no requiere programar nada. El proceso es:

1. **Registrar el gateway en la aplicación.** En *Administración → Gateways → Nuevo* se le da un nombre y una ubicación. La aplicación genera sus credenciales de forma automática.
2. **Descargar el archivo de configuración.** Al registrarlo, la aplicación entrega un archivo con todo lo que el gateway necesita: dirección del servidor, credenciales, certificado de seguridad, red y reloj. La contraseña se muestra una única vez y no queda guardada en texto legible en el servidor.
3. **Cargar la configuración en el gateway.** Se conecta el gateway por USB a una computadora y, con la herramienta de configuración de FireFly, se carga el archivo en un solo paso. El gateway la guarda y se reinicia.
4. **Conectarlo a la red.** Se conecta el cable Ethernet con PoE. El gateway obtiene su dirección automáticamente, se conecta al servidor y aparece **en línea** en la aplicación.
5. **Incorporar las luminarias.** Con el gateway abierto a nuevas incorporaciones, cada luminaria con su módulo FireFly se une a la red al energizarse y aparece sola en la aplicación. Solo queda asignarle nombre y ubicación.

![Lista de gateways en la sección Administración](img/presentacion/07-gateways.png)

*Figura 10. Administración de gateways: estado, última comunicación y acceso a sus datos de conexión.*

![Datos de conexión del gateway con la descarga del archivo de configuración](img/presentacion/08-gateway-configuracion.png)

*Figura 11. Datos de conexión del gateway y descarga del archivo de configuración.*

![Gateway FireFly con conector de red y PoE](img/gw_box.png)

*Figura 12. Diseño del gateway FireFly con conexión Ethernet y alimentación PoE en la misma placa.*

## 7. Reportes

### Cómo se generan

![Pantalla de reportes con filtros, vista previa y descargas](img/presentacion/06-reportes.png)

*Figura 13. Reportes: se elige el alcance, se revisa la vista previa y se descarga el formato deseado.*

1. En **Reportes** se elige el alcance: sede, edificio, nivel, gateway, una luminaria concreta, estado y rango de fechas.
2. **Consultar reporte** muestra una vista previa con la cantidad de mediciones y de resultados de prueba del periodo.
3. Se descarga en el formato necesario:
   - **PDF mensual:** documento de presentación con gráficos, tablas y resumen.
   - **CSV de mediciones** y **CSV de pruebas:** el detalle completo, para análisis en Excel u otras herramientas.

Los usuarios con perfil *monitor* también pueden generar y descargar reportes, sin acceso a la configuración.

### Qué contiene el reporte PDF

| Reporte de desempeño · página 1 | Página 2 |
| :---: | :---: |
| ![Primera página del reporte de ejemplo](img/presentacion/reporte-ejemplo-1.png) | ![Segunda página del reporte de ejemplo](img/presentacion/reporte-ejemplo-2.png) |

*Figura 14. Reporte de ejemplo generado con datos de demostración.*

- **Cabecera:** periodo, área consultada (sede, edificio, nivel, gateway o luminaria), responsable de la emisión, fecha y código único del documento.
- **1. Gráficos de seguimiento:**
  - Evolución mes a mes del porcentaje de pruebas aprobadas.
  - Comparativa de resultados por tipo: aprobada, voltaje bajo, corriente baja u otros.
- **2. Tabla de promedios mensuales:** tensión, corriente, temperatura, porcentaje de pruebas aprobadas y su variación respecto al mes anterior.
- **3. Resumen general:**
  - Cantidad de mediciones y de luminarias.
  - Pruebas aprobadas sobre el total.
  - Mejor y peor mes.
  - Recomendaciones de seguimiento.
- **Anexos:** detalle de mediciones por luminaria y resultado individual de cada prueba, conservando la ubicación y la configuración que tenía la luminaria en ese momento. Si una luminaria se mueve de lugar, los reportes antiguos no cambian.

## 8. Capacidad y escalabilidad

| Concepto | Valor |
| --- | --- |
| **Luminarias por gateway** | **Hasta 50** |
| Gateways por instalación | Los necesarios: uno por planta o zona; todos reportan al mismo servidor |
| Ejemplo | 200 luminarias en 4 plantas → 4 gateways |
| Pruebas simultáneas | Una prueba remota a la vez por gateway; las pruebas programadas se escalonan solas |
| Duración de cada prueba | De 30 segundos a 5 minutos |
| Histórico | 12 meses de mediciones por luminaria |
| Espacio en el servidor | Unos 0,6 GB por luminaria y año en uso normal |

Cada gateway gestiona su propia red, así que la instalación crece agregando gateways.

## 9. Resultados del piloto

La solución funciona hoy en una instalación piloto con dos luminarias Opalux, un gateway y el servidor FireFly:

- Las dos luminarias reportan en vivo y se ven en la vista general, en su ficha y en el plano del edificio.
- **Pruebas programadas en funcionamiento:** el 18 de noviembre de 2026 el sistema ejecutó solo las dos pruebas planificadas:

| Luminaria | Hora programada | Duración | Muestras registradas | Resultado |
| --------- | --------------- | -------- | -------------------- | --------- |
| Lampara 1 | 20:30           | 2 min    | 25                   | Aprobada  |
| Lampara 2 | 21:00           | 3 min    | 37                   | Aprobada  |
  
- En ensayos de banco de 5 minutos se registraron 61 muestras de tensión, corriente y temperatura por prueba. Así se ve con detalle cómo responde la batería mientras alimenta la luminaria.

## 10. Estado del producto y próximos pasos

| Componente | Estado |
| --- | --- |
| Aplicación web y servidor | En funcionamiento en la instalación piloto |
| Módulo de la luminaria | Prototipo funcionando en dos luminarias Opalux 9101-4SMD |
| Comunicación y pruebas remotas o programadas | Validadas en el piloto |
| Gateway con Ethernet y PoE | Comunicación Ethernet en funcionamiento en el piloto; placa propia con PoE integrado en su primer lote de fabricación |

Próximos pasos:
- Validar el gateway definitivo con PoE.
- Ampliar el piloto a más luminarias para medir la cobertura de la red en malla en un edificio real.
- Adaptar el módulo a otros modelos de luminaria.

> **Nota:** las pruebas de FireFly evalúan el comportamiento de la batería durante el tiempo programado. El resultado «aprobada» es una señal de mantenimiento; no certifica por sí solo la autonomía total de la luminaria. Para ello se requiere un ensayo de descarga completo según el protocolo aplicable.
