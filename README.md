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
- includes an active MQTT bridge transport/command/discovery path (Phase 2 completed);
- includes a Matter bridge runtime path (Phase 3 completed).

## Current Scope And Roadmap

| Phase | Repository Status | Main Protocols |
|------|-------------------|----------------|
| Phase 1 | Completed | Zigbee, HTTP, mDNS |
| Phase 2 | Completed: MQTT transport, topics, commands, status path, Home Assistant discovery, and broker HIL smoke | MQTT |
| Phase 3 | Completed: runtime contract, snapshot semantics, command/update loop, robustness gates, and Matter host/HIL smoke | Matter-over-Thread/Wi-Fi |

## Phase 3 Closure Evidence

```bash
# 1) Full blocking local gate (includes target HAL test firmware build)
scripts/run_blocking_local_checks.sh

# 2) Focused Matter Phase 3 regression gate
scripts/run_matter_phase3_checks.sh

# 3) Real-gateway Matter HIL smoke
GW_BASE_URL=http://192.168.178.171 \
JOIN_SECONDS=30 \
MATTER_LOOP_CYCLES=2 \
scripts/run_gateway_matter_hil_smoke.sh
```

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
components/matter_bridge/ - Matter bridge (Phase 3 completed runtime path)
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
- `zigbee-gateway/devices/<short_addr>/power/set`

Implementation API: `components/mqtt_bridge/include/mqtt_topics.hpp`.

Current broker-facing policy:

- device publish QoS: `1` (`at least once`)
- retained publish: `true` for `availability`, `state`, and `telemetry`
- subscribed command topics:
  - `zigbee-gateway/devices/+/config`
  - `zigbee-gateway/devices/+/power/set`

Home Assistant MQTT Discovery:

- Discovery prefix: `homeassistant`
- Discovery payloads are published by `components/mqtt_bridge/mqtt_discovery.cpp`
- Retained discovery entities are generated per online device for:
  - power switch
  - temperature sensor (if available)
  - occupancy binary sensor (if available)
  - contact binary sensor (if available)
  - battery sensor (if available)

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

MQTT power command ingress:

- Topic: `zigbee-gateway/devices/<short_addr>/power/set`
- Payload:
  - `{"power_on":true}`
  - `{"power_on":false}`
- Bridge maps payload to `CoreCommandType::kSetDevicePower` and submits through the normal async ServiceRuntime command path.
- Invalid payloads are rejected before entering runtime ingress.

MQTT transport/config contract:

- Kconfig gate: `CONFIG_ZGW_MQTT_TRANSPORT_ENABLED`
- Broker URI: `CONFIG_ZGW_MQTT_BROKER_URI`
- Client id: `CONFIG_ZGW_MQTT_CLIENT_ID`
- Optional auth:
  - `CONFIG_ZGW_MQTT_USERNAME`
  - `CONFIG_ZGW_MQTT_PASSWORD`
- Keepalive:
  - `CONFIG_ZGW_MQTT_KEEPALIVE_SEC`

MQTT runtime/read-model observability:

- Web/API exposes MQTT transport status through `GET /api/network`
- `mqtt` object fields:
  - `enabled`
  - `connected`
  - `last_connect_error` (`none|disabled|init_failed|start_failed|subscribe_failed`)
  - `broker_endpoint`
- This status is service-owned read model data, not a direct HAL/web coupling.

Matter endpoint mapping contract:

- Device classes map to stable endpoints:
  - `temperature -> 10`
  - `occupancy -> 11`
  - `contact -> 12`
- Explicit per-device mapping overrides class default mapping.

Implementation API: `components/matter_bridge/include/matter_endpoint_map.hpp`.

Matter snapshot-to-attribute bridge contract:

- `MatterBridge::set_endpoint_map(...)` installs a bounded endpoint mapping table.
- `MatterRuntimeApi::build_matter_bridge_snapshot(...)` is the narrow service-owned read-model contract consumed by Matter bridge.
- `ServiceRuntimeApi` extends `MatterRuntimeApi`, so bootstrap wiring still uses one runtime instance without exposing the full service surface to Matter bridge code.
- `MatterBridge::sync_snapshot(const service::MatterBridgeSnapshot&)` translates normalized Matter bridge snapshot devices to attribute updates.
- Snapshot identity is `short_addr`; bridge deltas are field-based and deterministic.
- Revision-only snapshot changes do not emit attribute deltas if payload fields are unchanged.
- First active device appearance emits `availability=true` and `stale` status plus class-specific attributes present in snapshot payload.
- Device disappearance emits `availability=false`.
- `MatterBridge::drain_attribute_updates(...)` returns deterministic, bounded deltas for publishing.
- Per-sync update production is bounded by `kMatterMaxUpdatesPerSync`; overflow is truncated (with warning on target) to keep runtime stable under pressure.
- Translation keeps Matter-specific transport logic out of Core; Core layout does not leak directly into Matter consumers.

Matter command ingress contract:

- `MatterBridge::post_power_command(...)` forwards power commands through `MatterRuntimeApi` (`next_operation_request_id()` + `post_command(...)`).
- Matter bridge must not mutate Core/Registry directly and must not synthesize Core events by itself.
- Attribute deltas are emitted only from snapshot payload changes, not directly from command enqueueing.

Implementation API: `components/matter_bridge/include/matter_bridge.hpp`.

Matter HAL C ABI contract:

- `hal_matter_publish_attribute_update(const hal_matter_attribute_update_t*)` is the thin C boundary for normalized sensor attribute updates.
- `hal_matter_attribute_update_t` carries only transport data (`endpoint_id`, attribute kind, scalar value fields) and contains no business policy/state machine.
- `hal_matter.c` exposes only weak-hook transport seam for platform adapters; domain lifecycle policy stays out of HAL.

Implementation API: `components/app_hal/include/hal_matter.h`.
Module notes: `components/matter_bridge/README.md`.

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
- Gateway MQTT HIL smoke (`test/hil`): real broker publish/subscribe + join/power/remove scenario.
- Gateway Matter runtime HIL smoke (`test/hil`): real join/command loop/remove path with Matter bridge runtime feed active.

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

Gateway Matter runtime HIL smoke:

```bash
GW_BASE_URL=http://192.168.178.171 \
JOIN_SECONDS=30 \
MATTER_LOOP_CYCLES=2 \
scripts/run_gateway_matter_hil_smoke.sh
```

Matter Phase 3 focused local gate:

```bash
scripts/run_matter_phase3_checks.sh
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
