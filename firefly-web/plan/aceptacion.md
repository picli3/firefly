# Criterios de aceptación

1. Sin sesión: API, planos, reportes y SSE rechazan el acceso; solo login disponible. Monitor no muta configuración. Sesiones revocadas al desactivar o restablecer credenciales.
2. Jerarquía y asociaciones persistentes. Muestras conservan ubicación histórica aunque se mueva la lámpara.
3. MQTT: aislamiento entre gateways, validación de payload/topic, duplicados ignorados, mensajes antiguos conservados sin sobrescribir último estado, reinicios distintos y fechas de recepción separadas de medición.
4. Configuración: validación de umbrales, versiones inmutables, estado deseado/aplicado separado, reintento idempotente, rechazo y diferimiento, persistencia al reiniciar. El ACK debe proceder de aplicación en el nodo, no del broker.
5. DXF: rechazo de archivos inválidos y referencias externas no resueltas; zoom/desplazamiento; marcadores persistentes; reemplazo solo tras confirmación.
6. PDF y CSV coinciden con filtros; incluyen límites temporales, ubicación y perfil históricos, datos ausentes y advertencias sobre estimaciones.
7. Reiniciar/recrear servicios conserva datos; respaldo restaurado en PostgreSQL nuevo y archivos recuperados.
8. Pruebas reales pendientes: C6 Ethernet/MQTT, envío Zigbee de perfil al H2, reinicio y comparación de dos baterías. No cerrar con simulador.

11. Planos con gateways: ubicación física persistente, validación de nivel y pares de coordenadas; monitor sin permiso de colocación. Activación del reemplazo atómica para lámparas y gateways.
12. Colores de comunicación: lámpara verde solo con telemetría propia vigente y gateway disponible; gris sin datos, al vencer tres intervalos o ante caída del gateway. Actualización sin recarga y vencimiento local si se pierde SSE. Alertas independientes del color.

13. Papelera: monitor sin acceso ni mutación; retirar dependencias antes del padre y restaurar padres primero. Registros ocultos de inventario y planos, histórico conservado, telemetría sin resurrección y MQTT denegado para nuevas conexiones de gateways retirados. Envío y restauración comprobados desde Administración.

14. Prueba individual: administrador confirma 30 s, monitor no ejecuta, lámpara y gateway vigentes, AC/sensor válidos y capacidad anunciada. UUID y test_id correlacionan el resultado; expiración, deduplicación y reinicio sin reejecución. La aceptación física requiere cargar C6 y validar en banco (J04 pendiente).
