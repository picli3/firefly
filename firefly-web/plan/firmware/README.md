# Tareas para el chat de firmware

La web registra aquí los requisitos que necesitan implementación en ESP32-C6 o ESP32-H2. **Este chat solo desarrolla la web; el chat de firmware ejecuta estas tareas.**

| ID | Tarea | Estado | Responsable |
| --- | --- | --- | --- |
| [FW-001](FW-001-configuracion-bateria-h2-c6.md) | Configurar umbrales de batería en H2 mediante C6 y confirmar aplicación | Implementada y validada en 4 V; pendientes físicos documentados | Chat de firmware H2/C6 |

## Procedimiento

1. La web crea una tarea `FW-NNN-<descripcion>.md`, sin editar firmware.
2. El chat de firmware lee la tarea, verifica el código y contratos actuales y cambia su estado a **en curso**.
3. Si necesita cambiar el contrato web, deja la propuesta y los campos incompatibles en la tarea; no presupone que la web los admite.
4. Registra archivos modificados, compilaciones, pruebas físicas realizadas y pendientes. Distingue «implementado», «compilado», «cargado» y «verificado en hardware».
5. Marca **completada** solo cuando se cumplen los criterios, o deja los pendientes concretos y su estado. La web integra/verifica su parte a partir de esa devolución.

No enviar órdenes de prueba ni cargar firmware por el mero hecho de leer una tarea. Las autorizaciones para operar hardware se gestionan en el chat responsable.
