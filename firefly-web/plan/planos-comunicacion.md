# Gateways y comunicación en planos

Al entrar en Planos se selecciona automáticamente el primer plano activo disponible y su nivel. Si no hay activos, el administrador ve el primer borrador disponible. El monitor solo puede ver planos activos. Los selectores permiten cambiar de nivel o plano en cualquier momento.

El administrador selecciona un nivel y plano, elige «Gateways», selecciona un gateway y activa «Colocar o mover gateway en este nivel». Un clic guarda coordenadas sobre el plano activo. En un borrador las posiciones quedan temporales hasta confirmar su activación; se validan y guardan lámparas y gateways en la misma transacción. Monitor consulta y abre marcadores, pero no los mueve.

El gateway puede atender lámparas de otros niveles. Su cuadrado G representa dónde está instalado, no la ubicación de todos sus nodos. Azul indica comunicación reciente; gris indica sin conexión o desactivado. Cambiar la ubicación desde administración elimina coordenadas que pertenecían al lugar anterior.

Cada círculo de lámpara es verde si recibió telemetría viva reciente a través de su gateway y este está disponible. Es gris si nunca reportó, si transcurren más de tres intervalos de reporte o si su gateway queda desconectado. Por ejemplo, un intervalo de 60 segundos vence a los 180 segundos. El último dato y su fecha se conservan. «Activa» significa comunicación, no que esté iluminando.

Una insignia naranja muestra la última alerta recibida sin reemplazar el color de comunicación. Puede quedar una alerta histórica en una lámpara gris. El estado y la fecha están disponibles al seleccionar el marcador.

La web conoce recepción por la telemetría `live:true` reenviada al servidor; aún no recibe un reloj independiente de última recepción de radio del C6. Telemetría histórica, duplicados, mensajes de otro nodo o el heartbeat del gateway no refrescan la actividad de una lámpara. La caída del gateway impide confirmar a sus nodos, aunque su última lectura fuera reciente. Esto no diagnostica por sí solo una avería de lámpara o radio.

Los eventos autenticados actualizan estados cada cinco segundos. Un contador local mantiene el vencimiento si se corta el canal. Se verificó con MQTT simulado; el firmware C6 y la validación física continúan pendientes.
