<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Zigbee Gateway (ESP32-C6)

`zigbee-gateway_v5` is an embedded gateway project for ESP32-C6 based on ESP-IDF 5.5.x.
Focus: stable layered architecture, business-logic isolation from HAL, host-side testability, and strict CI quality gates.

## Author

- `Alex.K.`

## What The Project Does

- starts a Zigbee gateway on ESP32-C6;
- provides a Web UI and HTTP API for control;
- uses separated layers `core -> service -> app_hal`;
- prepares the base for MQTT (Phase 2) and Matter bridge (Phase 3).

## Current Scope And Roadmap

| Phase | Repository Status | Main Protocols |
|------|-------------------|----------------|
| Phase 1 | Completed | Zigbee, HTTP, mDNS |
| Phase 2 | Module structure prepared | MQTT |
| Phase 3 | Module structure prepared | Matter-over-Thread/Wi-Fi |

## Key Documents

- [ARCHITECTURE.md](ARCHITECTURE.md) - architecture, layers, design invariants.
- [CODING_GUIDELINES.md](CODING_GUIDELINES.md) - C/C++ implementation rules.
- [ARCH_COMPLIANCE_MATRIX.md](ARCH_COMPLIANCE_MATRIX.md) - machine-checkable architecture rules.
- [ADR_EXCEPTIONS.md](ADR_EXCEPTIONS.md) - temporary rule exceptions.
- [TEAM_WORKFLOW.md](TEAM_WORKFLOW.md) - team process and Definition of Done.
- [check_arch_invariants.sh](check_arch_invariants.sh) - local architecture gate.

## Repository Structure (Short)

```text
main/                 - app_main and firmware entry point
components/core/      - event bus, reducer, registry, commands
components/service/   - use-case managers, orchestration
components/app_hal/   - adapters to ESP-IDF/stacks (C ABI)
components/web_ui/    - web server, routes, handlers, assets
components/mqtt_bridge/   - MQTT bridge (Phase 2)
components/matter_bridge/ - Matter bridge (Phase 3)
test/host/            - core/service unit tests on host
test/integration/     - integration host tests for web/platform shims
test/target/          - target HAL tests for ESP32-C6
```

## `/api/devices` Snapshot Contract

`GET /api/devices` returns a consistent per-device snapshot. Each device object always includes:

- `short_addr`, `online`, `power_on`
- `reporting_state` (`unknown|interview_completed|binding_ready|reporting_configured|reporting_active|stale`)
- `last_report_at`, `stale`
- `temperature_c` (`number` or `null`)
- `occupancy` (`unknown|not_occupied|occupied`)
- `contact` object: `state`, `tamper`, `battery_low`
- `battery` object: `percent`, `voltage_mv` (`number` or `null`)
- `lqi` (`number` or `null`)
- `rssi` (`number` or `null`)
- `force_remove_armed`, `force_remove_ms_left`

## `POST /api/config/reporting` Contract

Per-device reporting override is updated asynchronously via ingress queue.

Request JSON fields:
- `short_addr` (`1..65534`)
- `endpoint` (`1..255`)
- `cluster_id` (`1..65535`)
- `min_interval_seconds` (`0..65535`)
- `max_interval_seconds` (`1..65535`, must be `>= min_interval_seconds`)
- optional: `reportable_change` (`uint32`)
- optional: `capability_flags` (`0..255`)

Behavior:
- invalid key/range payload is rejected with `400`;
- accepted payload returns `{"accepted":true}` and is applied during runtime queue drain.

## MQTT Topic Contract (Phase 2)

Stable topic naming under root `zigbee-gateway`:

- `zigbee-gateway/devices`
- `zigbee-gateway/state`
- `zigbee-gateway/devices/<short_addr>/state`
- `zigbee-gateway/devices/<short_addr>/telemetry`
- `zigbee-gateway/devices/<short_addr>/availability`
- `zigbee-gateway/devices/<short_addr>/config`

Implementation API: `components/mqtt_bridge/include/mqtt_topics.hpp`.

Telemetry payload serializer contract:

- `temperature_c` (`number` or `null`)
- `occupancy` (`unknown|not_occupied|occupied`)
- `contact` object: `state`, `tamper`, `battery_low`
- `battery` object: `percent`, `voltage_mv` (`number` or `null`)
- `lqi` (`number` or `null`)
- `rssi` (`number` or `null`)
- `stale` (`boolean`)
- `timestamp_ms` (`number`)

Implementation API: `components/mqtt_bridge/include/mqtt_serializer.hpp`.

Device sync publish behavior:

- `MqttBridge::sync_snapshot(...)` publishes only relevant deltas.
- New online device: publishes `availability=online` + `state` + `telemetry`.
- Snapshot field change: publishes only changed topic (`state` or `telemetry`).
- Device removed/offline: publishes `availability=offline`.
- Publications can be drained via `MqttBridge::drain_publications(...)`.

MQTT config command ingress:

- Topic: `zigbee-gateway/devices/<short_addr>/config`
- Payload fields:
  - `endpoint`, `cluster_id`
  - `min_interval_seconds`, `max_interval_seconds`
  - optional: `reportable_change`, `capability_flags`
- Bridge maps payload to `CoreCommandType::kUpdateReportingProfile` and submits via `ServiceRuntime`.
- Input bounds are validated; repeated identical command is idempotent (no extra queued write).

Matter endpoint mapping contract:

- Device classes map to stable endpoints:
  - `temperature -> 10`
  - `occupancy -> 11`
  - `contact -> 12`
- Explicit per-device mapping overrides class default mapping.

Implementation API: `components/matter_bridge/include/matter_endpoint_map.hpp`.

Matter snapshot-to-attribute bridge contract:

- `MatterBridge::set_endpoint_map(...)` installs a bounded endpoint mapping table.
- `ServiceRuntimeApi::build_matter_bridge_snapshot(...)` is the service-owned bridge read model for Matter consumers.
- `MatterBridge::sync_snapshot(const service::MatterBridgeSnapshot&)` translates normalized Matter bridge snapshot devices to attribute updates.
- `MatterBridge::drain_attribute_updates(...)` returns deterministic, bounded deltas for publishing.
- Translation keeps Matter-specific transport logic out of Core; Core layout does not leak directly into Matter consumers.

Implementation API: `components/matter_bridge/include/matter_bridge.hpp`.

Matter HAL C ABI contract:

- `hal_matter_publish_attribute_update(const hal_matter_attribute_update_t*)` is the thin C boundary for normalized sensor attribute updates.
- `hal_matter_attribute_update_t` carries only transport data (`endpoint_id`, attribute kind, scalar value fields) and contains no business policy/state machine.

Implementation API: `components/app_hal/include/hal_matter.h`.

## Requirements

- ESP-IDF `v5.5.x` (`ARCHITECTURE.md` pins `v5.5.2`);
- target: `esp32c6`;
- CMake `>=3.16`;
- Python 3 and `idf.py`;
- for host coverage: `gcovr`.

Minimum target hardware requirement: ESP32-C6 with at least 8 MB flash.

## Quick Start (Firmware Build/Flash)

```bash
# 1) Activate ESP-IDF environment (example path)
source ~/esp/esp-idf/export.sh

# 2) Set target and build firmware
idf.py set-target esp32c6
idf.py build

# 3) Flash and monitor (change port if needed)
idf.py -p /dev/ttyACM0 flash monitor
```

## Local Checks Before Push (Required)

```bash
# Canonical blocking local verification bundle
scripts/run_blocking_local_checks.sh
```

Strict local mode:

```bash
scripts/run_blocking_local_checks.sh --strict
```

Manual equivalent:

```bash
# Architecture invariants (blocking: high+medium)
bash ./check_arch_invariants.sh

# Host unit tests
cmake -S test/host -B build-host
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure

# Required migration smoke
ctest --test-dir build-host --output-on-failure -R test_config_manager_migration

# Target HAL build verification
idf.py -C test/target -B build-target-tests build
```

## Test Matrix

- Host unit tests (`test/host`): core/service business logic.
- Host integration tests (`test/integration`): web handlers/routes and platform shims.
- Target HAL tests (`test/target`): HAL verification on ESP32-C6.
- HIL smoke/full: run in CI on a self-hosted runner.
- Gateway Zigbee HIL smoke (`test/hil`): real gateway reboot/join/on-off/remove scenario.

Host integration:

```bash
cmake -S test/integration -B build-integration
cmake --build build-integration --parallel
ctest --test-dir build-integration --output-on-failure
```

Target HAL build:

```bash
idf.py -C test/target -B build-target-tests set-target esp32c6
idf.py -C test/target -B build-target-tests build
```

Gateway Zigbee HIL smoke:

```bash
GW_BASE_URL=http://192.168.178.171 scripts/run_gateway_zigbee_smoke.sh
```

## Code Coverage (Core + Service)

```bash
cmake -S test/host -B build-host \
  -DCMAKE_CXX_FLAGS="--coverage" \
  -DCMAKE_C_FLAGS="--coverage" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure

gcovr --root . --object-directory build-host \
  --filter 'components/core/.*' \
  --filter 'components/service/.*' \
  --exclude '.*CMakeFiles.*' \
  --gcov-ignore-errors=no_working_dir_found \
  --print-summary
```

HTML report (local):

```bash
mkdir -p coverage-report
gcovr --root . --object-directory build-host \
  --filter 'components/core/.*' \
  --filter 'components/service/.*' \
  --exclude '.*CMakeFiles.*' \
  --gcov-ignore-errors=no_working_dir_found \
  --html-details coverage-report/index.html
```

## CI

Main pipeline: [.github/workflows/ci.yml](.github/workflows/ci.yml)

Blocking jobs for merge:
- `firmware-build`
- `host-tests`
- `reporting-regression`
- `target-hal-tests-build`
- `target-hal-tests-hil-smoke`
- `architecture-invariants`
- `static-analysis`

### Reporting Regression Gate

`reporting-regression` is a dedicated blocking CI job for reporting lifecycle stability.

It runs the host regression subset:
- `test_service_reporting_manager`
- `test_service_reporting_retry`
- `test_service_reporting_stale`
- `test_service_reporting_flow`
- `test_service_reporting_profiles`
- `test_service_reporting_semantics`
- `test_service_reporting_faults`

## Typical Issues

```bash
# Full host test rebuild if incremental build is broken
rm -rf build-host
cmake -S test/host -B build-host
cmake --build build-host --parallel
```

If `idf.py flash` does not see the port, check the current serial device (`/dev/ttyACM*` or `/dev/ttyUSB*`) and access permissions.

## License

This project is licensed under **GNU Affero General Public License v3.0** (`AGPL-3.0-only`).

Full text: [LICENSE](LICENSE).
