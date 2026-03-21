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
| `INV-M013` | Medium | `ARCHITECTURE.md` (snapshot consistency for read APIs) | `components/web_ui/web_handlers_network.cpp`, `components/web_ui/web_handlers_config.cpp`, `components/web_ui/include/web_routes.hpp` | Web read APIs must use ServiceRuntime-owned snapshot builders and must not access `CoreRegistry` directly. |
| `INV-M014` | Medium | `CODING_GUIDELINES.md` (explicit concurrency-safe reads) | `components/service/include/service_runtime.hpp` | `ServiceRuntime::stats()` must return a snapshot by value, not a reference to mutable live state. |
| `INV-M015` | Medium | `ARCHITECTURE.md` (HAL boundary ownership) | `components/service/include/service_runtime.hpp` | Public ServiceRuntime header must not expose `hal_zigbee_*` types or include `hal_zigbee.h`. |
| `INV-M016` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (Immutable Core State / Single Writer ownership) | `components/service/*_manager.cpp` | Service managers must not call `snapshot_copy()` or `pin_current()` directly; they must consume ServiceRuntime-owned state fragments or snapshot helpers. |
| `INV-M017` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (HAL thin-wrapper / clean modularity) | `components/app_hal/hal_zigbee.c` | HAL Zigbee must not contain hardcoded target-device diagnostics or per-device suppression logic. |
| `INV-M018` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (clean production API surface) | `components/service/include/network_manager.hpp`, `components/service/include/service_runtime.hpp`, `components/web_ui/web_handlers_network.cpp` | Production network service/web contract must not expose raw credential debug operations, DTO fields, or routes. |
| `INV-M019` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (narrow app-facing facade / modular extension seams) | `components/web_ui`, `components/mqtt_bridge`, `components/service/include/service_runtime_api.hpp` | Web/MQTT consumers must depend on `ServiceRuntimeApi` facade instead of the concrete `ServiceRuntime` header. |
| `INV-M020` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (test-only seams separated from production API) | `components/service/include/service_runtime.hpp`, `components/service/include/service_runtime_test_access.hpp` | Production `ServiceRuntime` header must not expose macro-gated test hooks; test-only access must live in a separate test access header. |
| `INV-M021` | Medium | `ARCHITECTURE.md` (Phase 2 extension wiring / bootstrap-only app) | `main/app_main.cpp`, `components/mqtt_bridge/include/mqtt_bridge.hpp` | Production app must attach `ServiceRuntime` to MQTT bridge and start its runtime snapshot feed path. |
| `INV-M022` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (service-owned bridge read models / modular extension seams) | `components/service/include/service_runtime_api.hpp`, `components/mqtt_bridge` | MQTT bridge must not read raw `CoreState` through the service facade; it must consume a service-owned MQTT bridge snapshot DTO. |
| `INV-M023` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (service-owned bridge read models / modular extension seams) | `components/service/include/service_runtime_api.hpp`, `components/matter_bridge`, `main/app_main.cpp` | Matter bridge must consume a service-owned Matter snapshot DTO and be wired to `ServiceRuntime` from bootstrap. |
| `INV-M024` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (modular service internals / narrow orchestrator responsibilities) | `components/service/include/bridge_snapshot_builder.hpp`, `components/service/service_runtime.cpp` | Bridge DTO field mapping must live in a dedicated service helper; `ServiceRuntime` must not own MQTT/Matter DTO field mapping inline. |
| `INV-M032` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (modular service internals / read-model seam extraction) | `components/service/include/read_model_coordinator.hpp`, `components/service/include/service_runtime.hpp`, `components/service/service_runtime.cpp` | Read-model rebuild/invalidate policy must be driven through `ReadModelCoordinator` `on_*` notifications, and cached `DevicesApiSnapshot` publication/reads must also live there; `ServiceRuntime` must not keep legacy inline snapshot sync helpers. |
| `INV-M033` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (modular service internals / async result seam extraction) | `components/service/include/operation_result_store.hpp`, `components/service/include/service_runtime.hpp`, `components/service/include/service_runtime_api.hpp`, `components/service/service_runtime.cpp`, `components/web_ui/web_handlers_network.cpp` | Operation request-id generation, network/config result queueing, and network polling semantics must live in a dedicated `OperationResultStore`; web polling must use one centralized service poll-status contract instead of combining multiple runtime helpers. |
| `INV-M034` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (modular service internals / Zigbee lifecycle seam extraction) | `components/service/include/zigbee_lifecycle_coordinator.hpp`, `components/service/include/service_runtime.hpp`, `components/service/service_runtime.cpp`, `components/service/network_manager.cpp` | Zigbee join-window cache, duplicate join suppression, remove-device policy, and force-remove lifecycle coordination must live in a dedicated `ZigbeeLifecycleCoordinator`; `ServiceRuntime` and `NetworkManager` should delegate these operations instead of owning the lifecycle helpers inline. |
| `INV-M035` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (modular service internals / persistence seam extraction) | `components/service/include/state_persistence_coordinator.hpp`, `components/service/include/service_runtime.hpp`, `components/service/service_runtime.cpp` | Core-state persist/restore storage, restore-pending lifecycle, and persist scheduling must live in a dedicated `StatePersistenceCoordinator`; `ServiceRuntime` should delegate these persistence operations instead of owning the storage, flags, or flush policy inline. |
| `INV-M036` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (narrow extension seams / stable Matter path contract) | `components/service/include/matter_runtime_api.hpp`, `components/service/include/service_runtime_api.hpp`, `components/matter_bridge` | Matter bridge integration must depend on a narrow `MatterRuntimeApi` seam (snapshot feed + command ingress) and must not include or reference the full `ServiceRuntimeApi` surface directly. |
| `INV-M037` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (controlled side effects / single runtime command ingress) | `components/matter_bridge/include/matter_bridge.hpp`, `components/matter_bridge/matter_bridge.cpp` | Matter bridge power command loop must use `MatterRuntimeApi` request-id + `post_command` ingress and must not bypass service command flow by constructing Core command-request events directly. |
| `INV-M038` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (runtime robustness / bounded queues / idempotent lifecycle) | `components/matter_bridge/matter_bridge.cpp` | Matter bridge lifecycle must remain idempotent (`start` guard) and update buffering must stay bounded (`kMatterMaxUpdatesPerSync`) with stop-path wake-up logic to reduce delayed-task shutdown races. |
| `INV-M039` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (HAL thin-wrapper + documented local SDK exceptions) | `components/matter_bridge/README.md`, `components/app_hal/hal_matter.c` | Matter module must document local RTTI/exception policy and ADR workflow; `hal_matter` must stay a thin transport adapter seam (weak hooks only) without Core/Service coupling or domain lifecycle logic. |
| `INV-M040` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (Single Writer + bounded memory in bridge runtime path) | `components/matter_bridge` | Matter bridge must avoid heap allocations in runtime path and must not read/mutate `CoreRegistry` directly (`pin_current/snapshot_copy/CoreRegistry` markers are forbidden). |
| `INV-M025` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (clean HAL boundary / test seam separation) | `components/app_hal/include/hal_zigbee.h`, `components/app_hal/include/hal_zigbee_test.h` | Production HAL Zigbee header must not expose test-only hooks or simulation APIs; test-only Zigbee access must live in a dedicated test header. |
| `INV-M026` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (HAL thin-wrapper / transport boundary ownership) | `components/app_hal/hal_mqtt.c`, `components/mqtt_bridge`, `components/service` | ESP-IDF MQTT client usage must stay inside HAL MQTT transport adapter; bridge/service layers must not include `mqtt_client.h` or call `esp_mqtt_client_*` directly. |
| `INV-M027` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (service-owned read models / clean web boundary) | `components/service/include/service_runtime_api.hpp`, `components/web_ui` | MQTT transport status shown in the web UI must come from a service-owned `NetworkApiSnapshot` read model, not direct reads from HAL transport or bridge code. |
| `INV-M028` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (test seam separation / clean bridge API) | `components/mqtt_bridge/include/mqtt_bridge.hpp`, `components/mqtt_bridge/include/mqtt_bridge_test_access.hpp` | Production MQTT bridge header must stay free of macro-gated test hooks; test-only MQTT bridge access must live in a dedicated test access header. |
| `INV-M029` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (extension isolation / adapter-specific concerns) | `components/mqtt_bridge/mqtt_discovery.cpp`, `components/core`, `components/service`, `components/web_ui` | Home Assistant discovery topic/payload specifics must live only in the MQTT bridge discovery layer, not in Core/Service/Web layers. |
| `INV-M030` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (clean service-to-web DTO boundary) | `components/service/include/service_runtime_api.hpp`, `components/web_ui/web_handlers_device.cpp` | `/api/devices` must use service-owned device DTOs; the service facade and web handler must not expose or serialize raw `CoreState` layout. |
| `INV-M031` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (narrow orchestrator responsibilities / modular snapshot mapping) | `components/service/include/devices_api_snapshot_builder.hpp`, `components/service/service_runtime.cpp` | `/api/devices` DTO field mapping must live in a dedicated service helper; `ServiceRuntime` should delegate this mapping instead of owning the DTO population logic directly. |
| `INV-M009` | Medium | `ARCHITECTURE.md` + `README.md` (quality gates) | `.github/workflows/ci.yml` | CI must include blocking `reporting-regression` job that runs dedicated reporting lifecycle tests. |
| `INV-M041` | Medium | `ARCHITECTURE.md` (layer dependency direction) | `components/app_hal`, `components/service/include` | `app_hal` must not include any service-layer headers; dependency flows downward from service to HAL, not upward. |
| `INV-M042` | Medium | `ARCHITECTURE.md` (layer dependency direction) | `components/core`, `components/service/include` | `core` must not include any service-layer headers; core is the innermost platform-agnostic layer with no upward dependencies. |
| `INV-M043` | Medium | `ARCHITECTURE.md` (layer dependency direction) | `components/core` | `core` must not include any HAL-layer headers (`hal_*.h`); core must remain platform-agnostic with no platform adapter coupling. |
| `INV-M044` | Medium | `ARCHITECTURE.md` + `CODING_GUIDELINES.md` (layer dependency direction / clean adapter boundary) | `components/matter_bridge` | Matter bridge must not include core headers (`core_*.hpp`) or use `core::` symbols directly; all core data must flow through service-owned DTOs. |
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
