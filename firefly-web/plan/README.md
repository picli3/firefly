# Plan de desarrollo

La aplicación y sus servicios viven en `firefly-web/`. No se publicará en GitHub en esta etapa.

- [Requisitos](requisitos.md)
- [Tareas y seguimiento](tareas.md)
- [Arquitectura y contrato MQTT](arquitectura.md)
- [Aceptación](aceptacion.md)
- [Verificación y límites](verificacion.md)
- [Integración firmware pendiente](firmware.md)

Orden: planificación → base → acceso → inventario y perfiles → planos → supervisión → reportes → integración. Los cambios de firmware corresponden a sus proyectos existentes y requieren compilación ESP-IDF y hardware para cerrar aceptación.

Estado: 69 de 75 tareas completadas. E01 y E05 en curso; E02–E04 bloqueadas por la integración firmware/hardware. J04 pendiente de prueba física individual.

- [Registro completo del gateway y contrato AT propuesto](registro-gateway.md)

- [Plantilla mensual, indicadores y marca](plantilla-reportes.md)

- [Gateways en planos y colores de comunicación](planos-comunicacion.md)

- [Pruebas individuales por MQTT](pruebas-individuales.md)

## Coordinación con el chat de firmware

Este chat desarrolla únicamente la web y registra las necesidades de C6/H2 en [Tareas para firmware](firmware/README.md). La tarea actual es [FW-001 — Configuración de batería del H2 mediante C6](firmware/FW-001-configuracion-bateria-h2-c6.md). El otro chat implementa y devuelve allí el contrato final y las verificaciones.

- [Despliegue en Orange Pi con NVMe](despliegue-orangepi.md)
- [FW-002 — Cambio del broker en el C6](firmware/FW-002-migracion-broker-orangepi.md)
