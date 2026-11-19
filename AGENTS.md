# Repository Guide

## System Boundaries

- `firmware/` contains four independent ESP-IDF projects; there is no root firmware build. Run `idf.py` from the specific project directory.
- The primary direct-Zigbee path is `h2-emergency-light` (end device) plus `c6-zigbee-gateway` (coordinator/logger). Both consume `firmware/common/include/emergency_protocol.h` through `EXTRA_COMPONENT_DIRS`.
- `h2-coordinator` plus `s3-gateway` is the alternate Zigbee-to-UART-to-MQTT path. These two currently duplicate the packed telemetry struct and framing constants in their `main.c`; a protocol change must keep them synchronized with `common` and `firmware/PROTOCOL.md`.
- Firmware entrypoints are each project's `main/main.c`. The emergency-light behavior is split across `battery_test`, `ina226`, `ntc`, `relay`, `telemetry`, and `zigbee_node`; the C6 gateway delegates to `node_registry`, `test_campaign`, and `zigbee_gateway`.

## ESP-IDF Workflow

- Required toolchain is ESP-IDF v5.5. Zigbee projects constrain IDF to `>=5.5.0,<5.6.0`; their tracked `dependencies.lock` files resolve `esp-zboss-lib` 1.6.4 and `esp-zigbee-lib` 1.6.8.
- `idf.py` is not available in a fresh shell. First run `source ~/esp/esp-idf/export.sh`.
- Build only from one of `firmware/h2-emergency-light`, `firmware/h2-coordinator`, `firmware/c6-zigbee-gateway`, or `firmware/s3-gateway`:

```sh
idf.py set-target esp32h2   # either H2 project
idf.py set-target esp32c6   # C6 gateway
idf.py set-target esp32s3   # S3 gateway
idf.py build
```

- `sdkconfig` and `managed_components/` are local/generated and ignored. Put reproducible defaults in `sdkconfig.defaults`; never commit S3 Wi-Fi/MQTT credentials from `sdkconfig`.
- There is no CI, unit-test target, lint config, or formatter config. The minimum focused verification is `idf.py build` for each affected project. Changes to the shared protocol require builds of both H2/C6 consumers and a compatibility review/build of the duplicated H2-coordinator/S3 path.
- Zigbee network state is persisted in NVS. Integration tests against a different coordinator may require `idf.py -p <port> erase-flash flash monitor`; this also erases schedules and other persisted state.
- C6 campaign timing is configured under `idf.py menuconfig` -> `Automatic battery tests`. It uses time since boot, not civil time; changing the first delay to 1 minute is the documented bench shortcut.

## Hardware Contract

- Treat `emergency-light-pcb/REPORTE_REVISION_CIRCUITO.md`, the KiCad netlist, and firmware constants as higher-value sources than prose pinout descriptions. The current `h2-emergency-light/README.md` still contains stale I2C, shunt, and mains-relay assumptions.
- Confirmed relay intent: K1's NC contact switches the lamp controller's `+7.5V` signal, not mains. Energizing K1 opens that signal to simulate AC loss while the INA226 measures battery operation. Do not redesign this as a 220 VAC path based only on the firmware README.
- Intended I2C routing is SDA=GPIO4 and SCL=GPIO12. The current firmware constants are reversed and must be aligned to hardware.
- R5 is 2 mOhm. If current LSB remains 100 uA, INA226 CAL is 25600 (`0x6400`), not the firmware's current 5120 value for 10 mOhm.
- Before touching the relay driver, verify package pin numbers: the selected `BSS138LT1G` is 1=G, 2=S, 3=D, while the current `Q_NMOS_GDS` design assigns 1=G, 2=D, 3=S.

## KiCad Workflow

- The board is a KiCad 10 project rooted at `emergency-light-pcb/emergency-light-pcb.kicad_pro`; `INA226.kicad_sch` is a hierarchical sheet.
- Never infer PCB connectivity from the schematic alone. After schematic changes, update the PCB from the schematic and run DRC with `--schematic-parity`; the current tree is known to contain schematic/PCB parity errors and is not fabrication-ready.
- Keep generated reports outside the repository unless explicitly requested. From `emergency-light-pcb/`, use:

```sh
kicad-cli sch erc --format report --units mm --severity-all \
  --output /tmp/emergency-light-erc.rpt emergency-light-pcb.kicad_sch
kicad-cli pcb drc --format report --units mm --severity-all --all-track-errors \
  --schematic-parity --refill-zones --output /tmp/emergency-light-drc.rpt \
  emergency-light-pcb.kicad_pcb
```

- Add `--exit-code-violations` when a clean ERC/DRC is expected. The current audited baseline has known violations, so inspect the report rather than treating the nonzero result as a tool crash.
- Do not generate fabrication outputs until parity is clean and the blockers in `REPORTE_REVISION_CIRCUITO.md` are resolved, especially the stale direct 7.5 V `/AC_STATUS` PCB route and Q1 pinout.
