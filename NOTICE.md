# Avisos de terceros

FireFly es un proyecto propietario de Maykol Rey: ver [LICENSE](LICENSE). Este archivo enumera los componentes de terceros que el proyecto usa o incluye. Cada uno conserva su propia licencia, y la licencia de FireFly no limita los derechos que esas licencias conceden.

## Incluidos en este repositorio

| Componente | Ubicación | Licencia |
| --- | --- | --- |
| Fuentes DejaVu Sans | `firefly-web/services/assets/DejaVuSans*.ttf` | Licencia de fuentes DejaVu, derivada de Bitstream Vera (texto completo en `firefly-web/services/assets/FONT-LICENSE.txt`) |
| Plugin de Obsidian *Automatic Table of Contents* (Johan Satgé) | `.obsidian/plugins/automatic-table-of-contents/` | Según su repositorio de origen |
| Plugin de Obsidian *SVG Viewer* (Konstantinos Gkilas) | `.obsidian/plugins/svg-viewer/` | Según su repositorio de origen |

Los plugins de Obsidian solo sirven para ver y exportar la documentación. No forman parte del producto.

## Firmware (ESP32-H2 y ESP32-C6)

Las dependencias se descargan al compilar (`managed_components/`) y no se guardan en el repositorio.

| Componente | Licencia |
| --- | --- |
| ESP-IDF (Espressif Systems) | Apache License 2.0 |
| `espressif/esp-zigbee-lib` | Apache License 2.0 |
| `espressif/esp-zboss-lib` (DSR Corporation y Espressif Systems) | Licencia propia de tipo BSD. Permite redistribuir los binarios integrados en un producto con chip de Espressif o en sus actualizaciones de software, y prohíbe aplicar ingeniería inversa a los binarios. |

## Aplicación web (dependencias directas de `firefly-web/package.json`)

| Paquete | Licencia |
| --- | --- |
| nuxt, vue, vue-router, h3, @nuxt/ui, tailwindcss | MIT |
| mqtt, pdfkit, svg-to-pdfkit, dxf-parser, zod | MIT |
| postgres (postgres.js) | Unlicense |
| @iconify-json/lucide (iconos Lucide) | ISC |
| @fontsource-variable/dm-sans, @fontsource-variable/manrope | SIL Open Font License 1.1 |

Las dependencias transitivas y sus licencias están en `firefly-web/package-lock.json` y en `node_modules/<paquete>/package.json`.

## Servicios del servidor (imágenes Docker sin modificar)

Se descargan de sus registros oficiales al desplegar y no se redistribuyen en este repositorio.

| Imagen | Software | Licencia |
| --- | --- | --- |
| `postgres:17-alpine` | PostgreSQL | PostgreSQL License |
| `eclipse-mosquitto:2` | Eclipse Mosquitto | Eclipse Public License 2.0 / Eclipse Distribution License 1.0 |
| `caddy:2-alpine` | Caddy | Apache License 2.0 |
| `node:22-bookworm-slim` | Node.js | MIT |
| `alpine/openssl` | OpenSSL | Apache License 2.0 |

## Diseños de hardware (KiCad)

Los esquemáticos usan símbolos y huellas de las bibliotecas oficiales de KiCad, publicadas bajo CC BY-SA 4.0 con una excepción expresa. Esa excepción permite usarlas en diseños propios sin que la licencia de la biblioteca se extienda al diseño. Los símbolos y huellas creados por el autor forman parte de FireFly.

---

Revisado el 2026-11-19 a partir de los archivos del repositorio. Antes de distribuir el producto, se recomienda volver a revisar las licencias de las dependencias en sus versiones vigentes.
