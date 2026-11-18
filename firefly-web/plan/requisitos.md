# Requisitos confirmados

Aplicación Nuxt privada para una organización, con sedes, edificios y niveles. Docker Compose: web/API, PostgreSQL, Mosquitto, worker y HTTPS.

Administrador: usuarios, gateways, lámparas, ubicaciones, DXF y perfiles de batería. Monitor: lectura y PDF/CSV. Sin registro público. Todo dato, archivo y canal en vivo exige sesión.

Un DXF 2D activo por nivel; colocación, movimiento, zoom, desplazamiento y descripción de ubicación. Reemplazo con borrador y revisión de posiciones. Sin dibujo de zonas ni editor CAD.

Estados independientes: comunicación, alimentación, alertas, sensor y prueba. Históricos y resultados originales con ubicación y configuración histórica. Porcentaje del H2 es estimación de tensión, no capacidad ni autonomía; los umbrales fijos de 6 V no diagnostican baterías de 4 V.

Perfiles por tipo, química y tensión nominal con atención, alerta, crítico, mínimo de prueba y referencias porcentuales. Versiones inmutables, aplicación explícita por administrador, persistencia en H2 y ACK de aplicación. Cambio durante prueba se difiere. Sin ACK no se declara aplicado.

Iniciales: español, America/Lima, 12 meses de mediciones, resultados de pruebas sin borrado automático, desconexión tras tres intervalos esperados. Sin importación CSV ni campañas manuales, múltiples organizaciones, SNMP/LibreNMS ni notificaciones externas.

Hardware actual: H2 router Zigbee; C6 guarda CSV y usa USB nativo. MQTT y perfiles remotos no están implementados en el firmware vigente. La ruta H2 coordinador/S3 no forma parte del producto.

Los planos deben ubicar también físicamente los gateways. El color principal de cada lámpara representa recepción reciente de su propia telemetría: verde activa y gris inactiva. No representa encendido ni el resultado eléctrico. Una alerta se muestra por separado. El gateway tiene un marcador distinto; recibir datos de otro nodo no reactiva una lámpara silenciosa.

Administración dispone de papelera restaurable para lámparas, gateways y ubicaciones (sede, edificio, nivel), con confirmación, auditoría y protección de dependencias. Solo administrador retira/restaura. No hay borrado definitivo; se conservan históricos y la telemetría no vuelve a dar de alta un registro retirado.

El administrador puede solicitar una prueba individual de batería de 30 segundos desde la ficha, con confirmación explícita, estado y resultado correlacionado. Requiere gateway con firmware MQTT de pruebas y reloj sincronizado, comunicación vigente, AC y sensor válido. No se reejecutan solicitudes inciertas ni vencidas. Monitor solo consulta.
