# Tareas de FireFly Web

Estados: **pendiente**, **en curso**, **bloqueada**, **completada**. Una casilla solo se marca tras verificar el criterio.

Creado antes de iniciar el código de la aplicación. El hardware real no se sustituye con simulaciones.

## Planificación

- [x] **P01 — Registrar requisitos confirmados, exclusiones y valores iniciales** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: —. Aceptación: Requisitos contrastados con el alcance aprobado.
- [x] **P02 — Documentar arquitectura, entidades y permisos** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: P01. Aceptación: Arquitectura y matriz de roles disponibles.
- [x] **P03 — Definir contrato MQTT de telemetría, disponibilidad y configuración** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: P02. Aceptación: Contrato versionado con ejemplos y confirmaciones.
- [x] **P04 — Registrar criterios de aceptación y dependencias del firmware** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: P03. Aceptación: Escenarios de aceptación y límites de hardware documentados.

## Base y despliegue

- [x] **B01 — Inicializar Nuxt con TypeScript** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: P04. Aceptación: Compilación de producción correcta.
- [x] **B02 — Configurar PostgreSQL y migraciones** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B01. Aceptación: Migración ejecutada sobre PostgreSQL.
- [x] **B03 — Preparar Mosquitto con autenticación y permisos por gateway** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: P03. Aceptación: Clientes no autorizados y topics ajenos rechazados.
- [x] **B04 — Crear servicio de procesamiento MQTT** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B02,B03. Aceptación: Mensajes válidos persistidos e inválidos rechazados.
- [x] **B05 — Preparar Docker Compose, HTTPS y volúmenes persistentes** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B01,B03. Aceptación: Compose validado y aplicación accesible por HTTPS.
- [x] **B06 — Documentar variables de entorno sin incluir secretos** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B05. Aceptación: Ejemplo y configuración documentados.

## Acceso privado

- [x] **A01 — Implementar inicio y cierre de sesión** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B02. Aceptación: Sesión y cierre verificados.
- [x] **A02 — Implementar roles administrador y monitor** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A01. Aceptación: Permisos comprobados por API.
- [x] **A03 — Proteger API, archivos, reportes y actualizaciones en vivo** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A02. Aceptación: Acceso anónimo rechazado.
- [x] **A04 — Implementar creación, desactivación y restablecimiento de usuarios** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A02. Aceptación: Cuentas y sesiones revocadas correctamente.
- [x] **A05 — Preparar creación segura del primer administrador** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A01. Aceptación: Alta por CLI sin registro público.

## Inventario y ubicaciones

- [x] **I01 — Administrar sedes, edificios y niveles** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A02. Aceptación: Jerarquía y relaciones verificadas.
- [x] **I02 — Registrar, editar y desactivar gateways** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: I01. Aceptación: Gestión y credenciales MQTT por gateway verificadas.
- [x] **I03 — Registrar lámparas y asignar gateway, batería y ubicación** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: I02. Aceptación: Asignaciones persistentes.
- [x] **I04 — Gestionar dispositivos detectados pendientes de asignación** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B04. Aceptación: Descubrimiento visible sin pérdida de muestras.
- [x] **I05 — Conservar historial de cambios de ubicación** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: I03. Aceptación: Muestras conservan ubicación de origen.

## Perfiles de batería

- [x] **C01 — Administrar perfiles y validar sus parámetros de voltaje** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A02. Aceptación: Valores incoherentes rechazados.
- [x] **C02 — Versionar perfiles y asignarlos por lámpara** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: C01,I03. Aceptación: Edición crea versión sin envío automático.
- [x] **C03 — Implementar solicitudes de configuración mediante gateway** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: C02,B04. Aceptación: Orden versionada emitida vía outbox.
- [x] **C04 — Mostrar configuración solicitada y aplicada, rechazos y pendientes** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: C03. Aceptación: ACK de lámpara necesario para aplicar.
- [x] **C05 — Implementar reintentos idempotentes y conciliación tras reconexión** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: C04. Aceptación: Órdenes antiguas no prevalecen.
- [x] **C06 — Registrar auditoría y detectar firmware incompatible** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: C04. Aceptación: Actor y capacidades visibles.
- [x] **C07 — Vincular cada medición y resultado con su configuración conocida** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B04,C04. Aceptación: Versión informada no se confunde con deseada.

## Planos

- [x] **D01 — Implementar carga privada y validación de DXF** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: I01,A03. Aceptación: DXF válido y elementos incompatibles identificados.
- [x] **D02 — Crear visor 2D con zoom y desplazamiento** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: D01. Aceptación: Geometría inspeccionada en navegador.
- [x] **D03 — Colocar y mover lámparas sobre el plano** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: D02,I03. Aceptación: Posición persiste al recargar.
- [x] **D04 — Abrir la ficha desde un marcador** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: D03. Aceptación: Navegación de marcador verificada.
- [x] **D05 — Implementar reemplazo de plano con revisión de posiciones** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: D03. Aceptación: Borrador no reemplaza plano activo sin confirmación.

## Supervisión

- [x] **S01 — Crear simulador MQTT reproducible** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: P03. Aceptación: Genera dispositivos, perfiles y fallos controlados.
- [x] **S02 — Validar y almacenar telemetría y resultados** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B04. Aceptación: Dos clases de mensaje almacenadas.
- [x] **S03 — Manejar duplicados, mensajes atrasados y reinicios** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: S02. Aceptación: Deduplicación y orden temporal probados.
- [x] **S04 — Calcular disponibilidad de gateways y lámparas** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: S02. Aceptación: Caída de gateway distinta de silencio de lámpara.
- [x] **S05 — Crear panel general, filtros y alertas visibles** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: S04. Aceptación: Filtros de jerarquía y estados operativos.
- [x] **S06 — Crear ficha de lámpara, gráficas e historial** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: S02. Aceptación: Detalle y series visibles.
- [x] **S07 — Implementar actualizaciones autenticadas en vivo** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: A03,S05. Aceptación: SSE con sesión validada durante conexión.
- [x] **S08 — Mostrar antigüedad, procedencia temporal y limitaciones del diagnóstico** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: S06. Aceptación: Datos atrasados y estimaciones etiquetados.

## Reportes

- [x] **R01 — Implementar filtros y vista previa** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: S06. Aceptación: Mismos filtros en pantalla y exportación.
- [x] **R02 — Generar informe PDF** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: R01. Aceptación: PDF renderizado y verificado.
- [x] **R03 — Exportar mediciones y resultados en CSV separados** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: R01. Aceptación: CSV válido, seguro y completo.
- [x] **R04 — Conservar trazabilidad de ubicación, perfil y datos ausentes** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: C07,I05,R03. Aceptación: Exportación con instantáneas históricas.
- [x] **R05 — Verificar permisos y coherencia entre pantalla y descargas** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: R02,R03. Aceptación: Pruebas para ambos roles y anónimo.

## Integración y entrega

- [ ] **E01 — Coordinar cambios de protocolo y firmware H2/C6** · Estado: en curso.
  - Evidencia / pendiente: Contrato y extensión Zigbee documentados en firmware.md; falta implementar y compilar firmware.
  - Depende de: P03,C05. Aceptación: Especificación y cambios firmware con compilación.
- [ ] **E02 — Verificar configuración persistente y confirmada en una lámpara real** · Estado: bloqueada.
  - Evidencia / pendiente: Depende de E01 y validación física de persistencia/ACK en H2.
  - Depende de: E01. Aceptación: Reinicio conserva perfil y ACK real.
- [ ] **E03 — Validar perfiles distintos en lámparas con baterías diferentes** · Estado: bloqueada.
  - Evidencia / pendiente: Depende de E02; perfiles sintéticos no validan baterías físicas.
  - Depende de: E02. Aceptación: Comparación real con límites apropiados.
- [ ] **E04 — Verificar recorrido completo hasta reporte descargado** · Estado: bloqueada.
  - Evidencia / pendiente: Depende de firmware MQTT/Ethernet y recorrido con hardware real.
  - Depende de: E03,R05. Aceptación: Evidencia de hardware y exportación.
- [ ] **E05 — Probar recuperación ante desconexiones y reinicios** · Estado: en curso.
  - Evidencia / pendiente: Recuperación del worker y persistencia del simulador verificadas; faltan desconexiones/reinicios de placas reales.
  - Depende de: C05,S03. Aceptación: Pruebas de servicio y hardware distinguidas.
- [x] **E06 — Implementar conservación de datos, respaldo y restauración** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B02. Aceptación: Restauración probada en volumen nuevo.
- [x] **E07 — Completar documentación de instalación y operación** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: B06. Aceptación: Guía reproducible y límites explícitos.
- [x] **E08 — Revisar cambios y preparar publicación posterior en GitHub** · Estado: completada.
  - Verificación: documentación o comportamiento contrastado; evidencia en [verificacion.md](verificacion.md).
  - Depende de: E07. Aceptación: Diff revisado sin secretos; sin publicar.

## Registro completo del gateway (ampliación)

- [x] **G01 — Separar dirección MQTT anunciada de la conexión interna** · Estado: completada.
  - Depende de: B03. Aceptación: host, puerto y TLS configurables; avisos de loopback y certificado. Evidencia: pruebas de gateway-provisioning.
- [x] **G02 — Entregar credenciales y certificado en una ventana emergente** · Estado: completada.
  - Depende de: I02,G01. Aceptación: contraseña de entrega única, copia y descargas, sin secretos en mensajes generales.
- [x] **G03 — Verificar credenciales contra Mosquitto y permitir renovación** · Estado: completada.
  - Depende de: G02. Aceptación: alta y rotación verificadas por conexión MQTT real; consulta posterior no recupera la clave.
- [x] **G04 — Generar paquete JSON y contrato AT provisional** · Estado: completada.
  - Depende de: G02. Aceptación: campos completos, fragmentación base64, SHA-256 y advertencia explícita de firmware pendiente.
- [x] **G05 — Aislar certificado público y documentar instalación LAN** · Estado: completada.
  - Depende de: G01. Aceptación: la web no monta la clave privada; renovación explícita y SAN DNS/IP documentados.
- [x] **G06 — Verificar la ventana y sus acciones en navegador** · Estado: completada.
  - Depende de: G02,G03,G04. Aceptación: modal visible tras alta, secreto oculto, cierre y consulta posterior; captura de evidencia.

## Plantilla mensual y marca (ampliación)

- [x] **T01 — Revisar y reproducir la plantilla mensual proporcionada** · Estado: completada.
  - Depende de: R02. Aceptación: A4, cabecera, metadatos, gráficos, tabla de doce meses y resumen contrastados con el PDF fuente.
- [x] **T02 — Definir y verificar los indicadores mensuales** · Estado: completada.
  - Depende de: T01,S02. Aceptación: unidades independientes, medias ponderadas, meses Lima, ausencia de datos y resultados originales sin inventar metas.
- [x] **T03 — Incorporar el SVG original a PDF, login, navegación y favicon** · Estado: completada.
  - Depende de: T01. Aceptación: geometría original, copias idénticas y logo vectorial en PDF.
- [x] **T04 — Verificar paginación y trazabilidad de los anexos** · Estado: completada.
  - Depende de: T02. Aceptación: reporte vacío, ejemplo y prueba de 14 páginas renderizados; acceso privado y descarga validados.
- [x] **T05 — Revisar la marca de la aplicación en navegador y cerrar documentación** · Estado: completada.
  - Depende de: T03,T04. Aceptación: logo visible tras despliegue y documentación de criterios disponible.

## Gateways y actividad en planos (ampliación)

- [x] **M01 — Guardar y auditar la ubicación física del gateway** · Estado: completada.
  - Depende de: I02,D03. Aceptación: migración versionada, nivel y coordenadas validados, API protegida y colocación comprobada desde el visor.
- [x] **M02 — Mostrar gateways y comunicación independiente por lámpara** · Estado: completada.
  - Depende de: M01,S04,S07. Aceptación: marcador G azul/gris, lámpara verde/gris por telemetría propia, alerta separada y vencimiento sin recargar.
- [x] **M03 — Verificar posiciones, permisos, estados en vivo y documentar criterios** · Estado: completada.
  - Depende de: M01,M02. Aceptación: diecisiete pruebas unitarias, siete escenarios API y recorrido en navegador con MQTT simulado; datos temporales retirados. Evidencia en [verificacion.md](verificacion.md).

## Acceso desde la LAN (ampliación)

- [x] **N01 — Habilitar acceso privado por IP desde la red local** · Estado: completada.
  - Depende de: B05,A03. Aceptación: bind HTTPS en `0.0.0.0`, origen LAN coherente, TLS por IP validado, login obligatorio y documentación de confianza de CA. Verificados login 200/API anónima 401 y sesión real por la IP del host; pendiente comprobación desde otro dispositivo físico.

## Papelera de inventario (ampliación)

- [x] **X01 — Implementar retirada y restauración privada del inventario** · Estado: completada.
  - Depende de: I01,I02,I03,A03. Aceptación: retirada reversible por registro, auditoría, bloqueo de dependencias y restauración ordenada; sin borrado físico ni pérdida de historial.
- [x] **X02 — Incorporar botones y sección Papelera en Administración** · Estado: completada.
  - Depende de: X01. Aceptación: envío con confirmación y restauración comprobados en navegador.
- [x] **X03 — Integrar papelera con MQTT, planos, reportes y permisos** · Estado: completada.
  - Depende de: X01,X02. Aceptación: nodo retirado no reaparece por telemetría, configuración pendiente cancelada, gateway sin autorización de nuevas conexiones, históricos conservados; pruebas aisladas y despliegue verificados.

## Pruebas individuales (ampliación)

- [x] **J01 — Añadir solicitud, permisos, seguimiento y resultado en la ficha** · Estado: completada.
  - Depende de: A03,S06. Aceptación: administrador confirma prueba de 30 s; monitor solo consulta; resultados correlacionados y estados explícitos.
- [x] **J02 — Implementar entrega MQTT sin repetición automática y recepción en C6** · Estado: completada.
  - Corrección de despliegue: sincronización de ACL desde worker independiente de cambios de contraseña. Verificado ACK de rechazo de una orden vencida en el C6 real, sin actuación; prueba eléctrica J04 aún pendiente.
  - Depende de: J01. Aceptación: UUID idempotente, expiración, NVS antes de actuar, deduplicación tras reinicio, IDs reservados y reporte MQTT; firmware compilado sin flashear.
- [x] **J03 — Verificar transporte, recuperación y recorrido de interfaz aislados** · Estado: completada.
  - Depende de: J01,J02. Aceptación: pruebas API/MQTT, prueba del parser C real con puertos de host, compilación ESP-IDF, TypeScript y recorrido de navegador.
- [ ] **J04 — Validar la prueba solicitada desde la web en hardware real** · Estado: pendiente.
  - Depende de: J03. Aceptación: cargar firmware C6 y comprobar relé, duración, rechazo, batería y resultado ante desconexión/reinicio. No se cargó firmware ni se actuó en placas en esta etapa.

## Dependencias asignadas al chat de firmware

La configuración de batería pendiente de E01–E03 se coordina mediante [FW-001](firmware/FW-001-configuracion-bateria-h2-c6.md), estado **pendiente**, responsable **chat de firmware H2/C6**. Es una descomposición de esas tareas, no una funcionalidad nueva ya implementada. Este chat no modifica firmware; adapta y verifica la web después de la devolución.

## Instalación en Orange Pi y NVMe

Instalación web ARM64, directorios, copia inicial, arranque systemd y verificaciones registrados en [despliegue-orangepi.md](despliegue-orangepi.md). El cambio de conexión del C6 se asigna exclusivamente al chat de firmware mediante [FW-002](firmware/FW-002-migracion-broker-orangepi.md). No se detuvo ni sustituyó la instalación de la PC.
