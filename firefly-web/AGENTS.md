# Alcance de trabajo de FireFly Web

## Separación acordada con el usuario

Estas reglas delimitan el trabajo del chat/agente encargado de la webapp y sus servicios en `firefly-web/`. Otro chat se encarga del firmware C6 y H2 y conserva la autorización del usuario para trabajar en esos proyectos y actualizar estados/evidencia en `plan/firmware/`.

- No editar, compilar ni cargar firmware desde este chat para resolver una necesidad de la web. Se puede leer para verificar contratos y compatibilidad.
- Toda dependencia de firmware debe registrarse primero en `plan/firmware/` como una tarea Markdown autocontenida, con identificador estable `FW-NNN`, estado, contrato web actual, cambios requeridos, criterios de aceptación y devolución esperada del chat de firmware.
- Enlazar la tarea en `plan/firmware/README.md`, `plan/README.md` y el apartado correspondiente de `plan/tareas.md`.
- El chat de firmware implementa y verifica sus cambios, y deja evidencia en la tarea. No atribuir soporte a un dispositivo por haberlo implementado en la web o en un simulador.
- Si firmware propone cambiar el contrato MQTT, registrar la propuesta y su incompatibilidad en la tarea antes de adaptar la web. Mantener compatibilidad con dispositivos anteriores y capacidades explícitas.
- No forzar capacidades, aplicación de perfiles ni resultados en PostgreSQL para ocultar soporte pendiente del firmware. Conservar visibles los límites actuales.
- Preservar los cambios de firmware ya existentes. La implementación de pruebas individuales hecha antes de este acuerdo es un antecedente, no autorización para seguir modificando firmware.

Tarea actual: `plan/firmware/FW-001-configuracion-bateria-h2-c6.md`.
