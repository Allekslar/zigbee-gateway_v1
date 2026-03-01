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
# Architecture invariants (blocking: high+medium)
bash ./check_arch_invariants.sh

# Host unit tests
cmake -S test/host -B build-host
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure

# Required migration smoke
ctest --test-dir build-host --output-on-failure -R test_config_manager_migration
```

Strict local mode:

```bash
ARCH_BLOCKING_SEVERITIES=high,medium,low bash ./check_arch_invariants.sh
```

## Test Matrix

- Host unit tests (`test/host`): core/service business logic.
- Host integration tests (`test/integration`): web handlers/routes and platform shims.
- Target HAL tests (`test/target`): HAL verification on ESP32-C6.
- HIL smoke/full: run in CI on a self-hosted runner.

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
- `target-hal-tests-build`
- `target-hal-tests-hil-smoke`
- `architecture-invariants`
- `static-analysis`

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
