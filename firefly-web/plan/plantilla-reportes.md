# Reportes basados en la plantilla mensual Firefly

Referencia visual: `Plantilla de Reporte Mensual - Firefly.pdf`, conservada sin cambios en esta carpeta. El servidor reproduce el diseño mediante PDFKit; no rellena ni modifica el PDF original en cada descarga. El SVG suministrado en `doc/firefly.svg` en la raíz del repositorio se copia sin alterar su geometría a los activos de la aplicación y del generador de PDF.

## Diseño

- A4, cabecera verde con logo vectorial y nombre Firefly en verde lima.
- Título «Reporte de Desempeño Mensual», código, período, área, responsable y emisión.
- Secciones originales: «1. Gráficos de Seguimiento», «2. Tabla de Promedios Mensuales» y «3. Resumen General».
- Un cuadro de doce meses por año incluido en el rango; los meses sin datos muestran `-`.
- Anexos de mediciones por lámpara/configuración y resultados individuales de pruebas; encabezados repetidos y pie «Página N de total».
- El reporte base sin datos tiene dos páginas. Los anexos o rangos con varios años agregan páginas, sin truncar resultados.

## Datos y criterios

| Elemento de la plantilla  | Valor utilizado                                                                                               |
| ------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Período                   | Mes/año o año/rango filtrado, presentado en America/Lima                                                      |
| Área                      | Nombres de sede, edificio, nivel, gateway y lámpara seleccionados                                             |
| Responsable               | Usuario autenticado que genera el documento; responsable de emisión                                           |
| Emisión / código          | Fecha de generación e identificador de la descarga                                                            |
| Evolución mensual         | Porcentaje de resultados originales `PASS` recibidos                                                          |
| Comparativa por categoría | PASS, LOW_VOLTAGE, LOW_CURRENT y otros resultados                                                             |
| Indicadores de la tabla   | Voltaje (V), corriente (mA), temperatura (°C), PASS (%) y variación PASS (pp)                                 |
| Promedio del período      | Medias ponderadas por cantidad de muestras válidas y tasa PASS calculada sobre todos los resultados recibidos |
| Resumen                   | Conteos, meses observados con mayor/menor tasa PASS y limitaciones de interpretación                          |

No se promedian voltios, miliamperios y grados entre sí. Cada columna mantiene su unidad. El intervalo de muestreo puede variar: las medias no son promedios ponderados por tiempo ni por lámpara. Una flota de baterías distintas puede tener tensiones diferentes; el anexo conserva los valores por equipo y perfil.

La variación en puntos porcentuales compara con el mes calendario inmediatamente anterior; si falta ese mes o no recibió pruebas se indica `-`. Una tasa del 0 % es distinta de un mes sin pruebas. No se inventa una meta o una línea objetivo. PASS conserva el criterio original del firmware y no certifica autonomía o iluminación.

Los registros con sensor no válido no aportan ceros a los promedios. Una fecha de medición fiable prevalece; en su ausencia se identifica la recepción. La agrupación de meses usa America/Lima, también para registros próximos a medianoche UTC. El rango y los filtros de la descarga siguen siendo los de la consulta; el diseño anual no añade datos de meses que no fueron consultados.

Se conservan ubicación histórica, revisión y perfil de cada registro en los anexos. Los CSV mantienen sus columnas y permisos. El PDF admite hasta 12 años por descarga y devuelve un error explícito para rangos mayores.

## Marca

Las dos copias del SVG (`public/branding/firefly.svg` y `services/assets/firefly.svg`) son idénticas al archivo proporcionado. El PDF representa la misma geometría con el color lima de la plantilla. La web utiliza el SVG en la barra lateral, el login y el favicon. El símbolo genérico anterior se retiró.

Si cambia el SVG maestro, actualizar ambas copias y revisar de nuevo el PDF y la aplicación. No se modifica el original al generar reportes.

## Verificación

`tests/report.test.mjs`: promedios y ponderación, límites de mes en Lima, sensores inválidos, perfiles/fechas desconocidos, tasas y variación PASS, ausencia de datos, varios años, límite de rango, identidad de los activos y generación PDF.

Verificación visual con Poppler: ejemplo con datos sintéticos de 2026, reporte vacío de dos páginas y prueba de paginación con 90 resultados y ubicaciones largas (14 páginas). La integración HTTPS comprueba también el PDF para monitor, acceso privado y rechazo del rango excesivo.

Ejemplo ilustrativo en `output/pdf/firefly-reporte-mensual-ejemplo.pdf`, ignorado por Git. Sus datos son sintéticos y están identificados como DEMOSTRACIÓN.
