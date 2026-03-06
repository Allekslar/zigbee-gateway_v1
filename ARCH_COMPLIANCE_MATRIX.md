<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Architecture Compliance Matrix

This file defines the architecture rules that are enforced by `check_arch_invariants.sh`.

## Severity policy

- `High`: hard-blocking in CI and local gate.
- `Medium`: hard-blocking in CI and local gate.
- `Low`: non-blocking by default (warning), can be made blocking with `ARCH_BLOCKING_SEVERITIES=high,medium,low`.

## Rule matrix

| Rule ID | Severity | Source | Scope | Automated check |
|---|---|---|---|---|
| `INV-H000` | High | Process governance | Repo root docs | Required governance artifacts exist (`ARCH_COMPLIANCE_MATRIX.md`, `ADR_EXCEPTIONS.md`). |
| `INV-H001` | High | `ARCHITECTURE.md` (Core platform-agnostic) | `components/core` | No ESP-IDF/platform includes in Core. |
| `INV-H002` | High | `CODING_GUIDELINES.md` (memory discipline) | `components/core`, `components/service` | No `malloc/calloc/realloc/free` in Core/Service. |
| `INV-H003` | High | `ARCHITECTURE.md` (bootstrap-only app) | `main/app_main.cpp` | `app_main` must not run runtime loop logic and must call `g_runtime.start(...)`. |
| `INV-M001` | Medium | `CODING_GUIDELINES.md` (hot-path heap avoidance) | `components/app_hal/hal_wifi.c` | No `calloc/free` use in Wi-Fi scan path file. |
| `INV-M002` | Medium | `ARCHITECTURE.md` (HAL thin-wrapper) | `components/app_hal/hal_zigbee.c` | Join dedup/auto-close policy markers must not exist in HAL Zigbee. |
| `INV-M003` | Medium | `ARCHITECTURE.md` (single-writer service runtime) | `components/service/service_runtime.cpp` | Runtime task entry and service-owned join ingress API must exist. |
| `INV-M004` | Medium | `CODING_GUIDELINES.md` (compile policy) | `cmake/ProjectCompileOptions.cmake` | Must enforce `-fno-exceptions`, `-fno-rtti`, `-fno-threadsafe-statics`. |
| `INV-M005` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (HAL boundary contract) | `components/app_hal/include/hal_wifi.h`, `components/app_hal/include/hal_nvs.h` | HAL Wi-Fi/NVS APIs must use typed status enums, not raw `int` return contract. |
| `INV-M006` | Medium | `ARCHITECTURE.md` (HAL thin-wrapper + Service policy ownership) | `components/app_hal/hal_wifi.c`, `components/service/service_runtime.cpp` | Wi-Fi scan/connect mode policy must live in ServiceRuntime, not HAL Wi-Fi. |
| `INV-M007` | Medium | `ARCHITECTURE.md` (snapshot consistency for read APIs) | `components/web_ui/web_handlers_device.cpp`, `components/service/service_runtime.*` | `/api/devices` must use ServiceRuntime-owned atomic devices API snapshot, not mixed registry/HAL reads. |
| `INV-M008` | Medium | `ARCHITECTURE.md` (HAL thin-wrapper + Zigbee policy ownership) | `components/app_hal/hal_zigbee.c`, `components/service/service_runtime.cpp` | Zigbee formation/join policy state machine must live in ServiceRuntime, not HAL Zigbee. |
| `INV-M010` | Medium | `ARCHITECTURE.md` (HAL thin-wrapper + Service policy ownership) | `components/app_hal/hal_zigbee.c`, `components/service/network_policy_manager.cpp`, `components/service/service_runtime.cpp` | Auto-rejoin policy markers must not exist in HAL; Service `NetworkPolicyManager` must own and trigger this policy. |
| `INV-M011` | Medium | `CODING_GUIDELINES.md` (platform include hygiene) | `components/service`, `components/app_hal` | FreeRTOS include order must be `freertos/FreeRTOS.h` before `freertos/task.h`. |
| `INV-M012` | Medium | `ARCHITECTURE.md` (single-writer/concurrency safety) | `components/service` | Service spinlock paths must not use `vTaskDelay(1)` busy waits. |
| `INV-M009` | Medium | `ARCHITECTURE.md` + `README.md` (quality gates) | `.github/workflows/ci.yml` | CI must include blocking `reporting-regression` job that runs dedicated reporting lifecycle tests. |
| `INV-L001` | Low | `CODING_GUIDELINES.md` (central log tags) | `components/common/include/log_tags.h` | Registry should contain runtime/HAL tags used by critical modules. |

## Exception mechanism

- File: `ADR_EXCEPTIONS.md`
- Format: machine-readable HTML comment per exception:
  - `<!-- ARCH_EXCEPTION: RULE=<RULE_ID> PATH=<regex> EXPIRES=<YYYY-MM-DD> STATUS=active ADR=<ADR-ID> -->`
- Expired exceptions are ignored automatically by the gate.

## Local usage

```bash
bash ./check_arch_invariants.sh
```

Strict mode (including low severity):

```bash
ARCH_BLOCKING_SEVERITIES=high,medium,low bash ./check_arch_invariants.sh
```
