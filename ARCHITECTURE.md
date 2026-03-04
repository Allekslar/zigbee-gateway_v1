<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# ARCHITECTURE.md — Zigbee Gateway (ESP32-C6, ESP-IDF 5.5.2)

> <mark>Implementation language: C++ (embedded profile, no exceptions), with C ABI at ESP-IDF/stack boundaries.</mark>  
> Platform: ESP32-C6, ESP-IDF v5.5.2  
> Build: CMake + idf.py (Ninja)
> <mark>Minimum target HW requirement: ESP32-C6 with at least 8 MB flash.</mark>

---

## 1. Project Overview

The application evolves through three phases:

| Phase | Description | Protocols |
|------|------|-----------|
| **Phase 1** | Zigbee Gateway + Web UI | Zigbee, HTTP, mDNS |
| **Phase 2** | + Zigbee2MQTT | + MQTT |
| **Phase 3** | + Matter Bridge | + Matter-over-Thread/Wi‑Fi |

---

## 2. Architecture Principles

1. **Layered Architecture** — clear layers with one-way dependencies (top to bottom).
2. **Dependency Inversion** — upper layers define interfaces, lower layers implement them.
3. **Event‑Driven Core** — communication through events (Event Bus).
4. **Single Responsibility** — each module has one responsibility.
5. **Open/Closed** — new functionality is added via new modules.
6. **Hardware Abstraction** — interaction with ESP‑IDF/drivers only through HAL adapters.
7. **Testability** — business logic is isolated from HAL and tested on host.
8. **Async‑First Command Execution** — device commands are asynchronous with result confirmation.
9. **Immutable Core State** — Core works as a state machine: `next = core_reduce(prev, event)`; state is not mutated in place.
10. **Single Writer** — exactly one writer context produces new snapshots; readers consume consistent snapshots without RW mutex.
11. **Controlled Side Effects** — Core produces *effects/intents*, while execution happens in Service/HAL.
12. **Data Durability** — NVS data is versioned and migrated during OTA.
13. <mark>**C ABI Boundaries** — integrations with stacks (Zigbee/Matter) are done through `extern "C"` wrappers; Core does not export C++ classes externally.</mark>
14. **Reporting Policy Resolution** — sensor reporting policy is resolved in Service layer as: `per-device override > device-class default (temperature/motion/contact)`.
15. **Telemetry Semantics in Service** — Zigbee raw reports are normalized in Service to domain telemetry events (e.g. `0x0402/measuredValue` in 0.01°C, with invalid marker handling), Core consumes only normalized payload.

---

## 3. Embedded C++ Rules (Pinned)

This section is **mandatory** for all project components to avoid architecture drift in Phase 3.

### 3.1 Compilation Configuration (Build Flags)

<mark>Recommended global flags (at component level via `target_compile_options`):</mark>

```cmake
# example: components/<name>/CMakeLists.txt
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
)
```

<mark>Explanation:</mark>
- `-fno-exceptions` — disable exceptions (predictable RAM/latency).
- `-fno-rtti` — disable RTTI when not needed.
- `-fno-threadsafe-statics` — avoid hidden static-init guards (initialization is controlled explicitly).

> If Matter SDK needs RTTI/exceptions in a specific submodule, this is allowed **locally**, but must be explicitly documented in `components/matter_bridge/README.md`.

### 3.2 Allowed/Forbidden C++ Features

**Forbidden:**
- `throw/catch`
- `dynamic_cast`, `typeid` (when `-fno-rtti`)
- uncontrolled `new/delete` (without policy)
- STL containers without bounded capacity (for example `std::vector` without `reserve()` and upper bounds)

**Allowed:**
- `enum class`, `constexpr`, `noexcept`
- RAII for resources (NVS handles, mutex guards, buffers)
- move semantics
- fixed-capacity structures (`std::array`, custom `static_vector<N>`)
- `std::span` (or custom analog) for zero-copy buffer passing

### 3.3 Memory Management Style

<mark>Base rule:</mark> **Core must not do fine-grained heap allocations per event.**

**Allowed memory sources:**
- Stack — local temporary objects
- Static/`constexpr` — tables, constants
- Pool/Arena — snapshots, registry entries, event payload buffers
- Heap — only for rare operations (initial init, loading large blobs from NVS), preferably through one controlled allocator

**Snapshot allocation policy (recommended for ESP32‑C6):**
- ring `N=4..8` snapshots or pool + refcount
- writer stores a new snapshot into a free slot
- readers hold snapshots briefly (during HTTP/MQTT response construction)

---

## 4. Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      APPLICATION LAYER                          │
│  ┌──────────┐  ┌──────────────┐  ┌────────┐  ┌──────────────┐  │
│  │ Web UI   │  │ MQTT Bridge  │  │ Matter │  │ CLI/Debug    │  │
│  │ (Phase 1)│  │ (Phase 2)    │  │ Bridge │  │ (optional)   │  │
│  └────┬─────┘  └──────┬───────┘  └───┬────┘  └──────┬───────┘  │
│       │               │              │               │          │
├───────┴───────────────┴──────────────┴───────────────┴──────────┤
│                       SERVICE LAYER                             │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────────┐ │
│  │ Device       │ │ Network      │ │ Config                   │ │
│  │ Manager      │ │ Manager      │ │ Manager                  │ │
│  └──────┬───────┘ └──────┬───────┘ └────────────┬─────────────┘ │
│         │                │                      │               │
├─────────┴────────────────┴──────────────────────┴───────────────┤
│                       CORE LAYER                                │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────────┐ │
│  │ Event Bus    │ │ State Store  │ │ Command                  │ │
│  │              │ │ (Snapshots)  │ │ Dispatcher               │ │
│  └──────────────┘ └──────────────┘ └──────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                     PLATFORM LAYER (HAL)                        │
│  ┌──────────┐ ┌────────┐ ┌───────┐ ┌─────┐ ┌──────┐ ┌───────┐ │
│  │ Zigbee   │ │ Wi-Fi  │ │ NVS   │ │SPIFF│ │ mDNS │ │ RCP   │ │
│  │ Stack    │ │ + AP   │ │ Store │ │ S   │ │      │ │ Update│ │
│  └──────────┘ └────────┘ └───────┘ └─────┘ └──────┘ └───────┘ │
├─────────────────────────────────────────────────────────────────┤
│                     ESP-IDF 5.5.2 / FreeRTOS                   │
│                     ESP32-C6 Hardware                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Directory Structure

<mark>Updated for C++: `.c/.h` → `.cpp/.hpp` for internal modules (except thin C wrappers).</mark>

```
zigbee-gateway/
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32c6
├── partitions.csv
├── README.md
├── ARCHITECTURE.md
│
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.cpp                 # <mark>entry: C++</mark>
│   └── Kconfig.projbuild
│
├── components/
│   ├── core/                        # ── CORE LAYER ──
│   │   ├── include/
│   │   │   ├── core_event_bus.hpp
│   │   │   ├── core_events.hpp
│   │   │   ├── core_state.hpp       # <mark>aggregated immutable state</mark>
│   │   │   ├── core_registry.hpp    # snapshot API
│   │   │   ├── core_commands.hpp
│   │   │   ├── core_effects.hpp     # <mark>effects/intents</mark>
│   │   │   └── core_errors.hpp
│   │   ├── core_event_bus.cpp
│   │   ├── core_registry.cpp
│   │   ├── core_reducer.cpp         # <mark>core_reduce(prev, event) → next + effects</mark>
│   │   ├── core_command_dispatcher.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── service/                     # ── SERVICE LAYER ──
│   │   ├── include/
│   │   │   ├── device_manager.hpp
│   │   │   ├── network_manager.hpp
│   │   │   ├── config_manager.hpp
│   │   │   └── effect_executor.hpp  # <mark>executes effects</mark>
│   │   ├── device_manager.cpp
│   │   ├── network_manager.cpp
│   │   ├── config_manager.cpp
│   │   ├── effect_executor.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── app_hal/                     # ── PLATFORM LAYER (HAL) ──
│   │   ├── include/
│   │   │   ├── hal_zigbee.h         # <mark>C ABI thin layer</mark>
│   │   │   ├── hal_matter.h         # <mark>C ABI thin layer</mark>
│   │   │   ├── hal_nvs.h
│   │   │   ├── hal_wifi.h
│   │   │   ├── hal_mdns.h
│   │   │   ├── hal_spiffs.h
│   │   │   ├── hal_rcp.h
│   │   │   └── hal_led.h
│   │   ├── hal_zigbee.c             # C wrapper
│   │   ├── hal_matter.c             # C wrapper (Phase 3)
│   │   ├── hal_nvs.c
│   │   ├── hal_wifi.c
│   │   ├── hal_mdns.c
│   │   ├── hal_spiffs.c
│   │   ├── hal_rcp.c
│   │   ├── hal_led.c
│   │   └── CMakeLists.txt
│   │
│   ├── web_ui/
│   │   ├── include/
│   │   │   ├── web_server.hpp
│   │   │   ├── web_routes.hpp
│   │   │   └── web_dto.hpp          # <mark>DTO for JSON</mark>
│   │   ├── web_server.cpp
│   │   ├── web_routes.cpp
│   │   ├── web_handlers_device.cpp
│   │   ├── web_handlers_network.cpp
│   │   ├── web_handlers_config.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── mqtt_bridge/
│   │   ├── include/
│   │   │   ├── mqtt_bridge.hpp
│   │   │   ├── mqtt_topics.hpp
│   │   │   └── mqtt_serializer.hpp
│   │   ├── mqtt_bridge.cpp
│   │   ├── mqtt_discovery.cpp
│   │   ├── mqtt_device_sync.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── matter_bridge/
│   │   ├── include/
│   │   │   ├── matter_bridge.hpp
│   │   │   └── matter_endpoint_map.hpp
│   │   ├── matter_bridge.cpp
│   │   ├── matter_device_map.cpp
│   │   └── CMakeLists.txt
│   │
│   └── common/
│       ├── include/
│       │   ├── log_tags.h           # ESP_LOG TAGs (C header ok)
│       │   ├── utils.hpp
│       │   └── version.hpp
│       ├── utils.cpp
│       └── CMakeLists.txt
│
├── web/
│   ├── index.html
│   ├── app.js
│   ├── style.css
│   └── build.sh
│
└── test/
    ├── host/
    │   ├── test_core_registry.cpp
    │   ├── test_core_reducer.cpp
    │   ├── test_core_effects.cpp
    │   └── CMakeLists.txt
    ├── target/
    │   ├── test_hal_nvs.c
    │   ├── test_hal_zigbee.c
    │   └── CMakeLists.txt
    └── mocks/
        ├── mock_hal_nvs.h
        ├── mock_hal_nvs.c
        ├── mock_hal_zigbee.h
        └── mock_hal_zigbee.c
```

---

## 6. Core Layer (Platform‑Agnostic, C++)

### 6.1 Core As A State Machine (Immutable)

<mark>Core produces the next state and a list of effects:</mark>

```cpp
struct CoreReduceResult {
    CoreState next;
    EffectList effects; // fixed-capacity or pool-backed
};

CoreReduceResult core_reduce(const CoreState& prev, const CoreEvent& ev) noexcept;
```

### 6.2 State Store (Registry/Config) — Snapshots

- readers take a snapshot and read without locks
- writer creates a new snapshot (copy‑on‑write / structural sharing)
- `atomic_store` switches the active version
- memory is reclaimed via ring/pool policy (see §3.3)

---

## 7. Service Layer (Side Effects)

Service Layer:
- subscribes to HAL events (Zigbee/Matter/Wi‑Fi)
- calls `core_reduce()`
- <mark>executes `effects` via `effect_executor`</mark>
- publishes events back to Event Bus when needed (for example `result success/timeout`)

---

## 8. HAL Layer (ESP‑IDF Integration)

HAL remains a thin wrapper over ESP‑IDF and protocol stacks.

<mark>Rule:</mark> **HAL API is C, so C++ types do not leak into external dependencies.**

Boundary example:

```c
// hal_zigbee.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_device_joined)(const uint8_t ieee[8], uint16_t short_addr);
    void (*on_attribute_report)(const uint8_t ieee[8], uint8_t ep,
                                uint16_t cluster, uint16_t attr,
                                const void *data, uint16_t len);
} hal_zigbee_callbacks_t;

esp_err_t hal_zigbee_init(const hal_zigbee_callbacks_t *cb);

#ifdef __cplusplus
}
#endif
```

The adapter in Service Layer converts callback data into `Event` for Core.

---

## 9. Concurrency (Aligned With Immutable State)

- One writer (Service task / event_bus_task) produces `next` snapshots
- Readers (HTTP/MQTT/Matter) consume snapshots without RW mutex
- Mutex/spinlock remain only for: queue, pending tables, NVS batch

---

## 10. Memory Management (RAM/Flash) — Immutable+C++ Update

### RAM Budget (ESP32‑C6: ~512KB)

> <mark>The budget includes snapshot ring/pool instead of `registry + mutex`.</mark>

| Component | Approximate Size |
|-----------|------------------|
| FreeRTOS + stacks | ~40 KB |
| Zigbee Stack | ~80 KB |
| Wi‑Fi | ~60 KB |
| HTTP Server | ~16 KB |
| <mark>State Store snapshots (64 devices, ring N=4)</mark> | <mark>~40–60 KB (depends on layout)</mark> |
| Event Bus (queue 32 events) | ~5 KB |
| Command Dispatcher (pending table) | ~2 KB |
| MQTT Client (Phase 2) | ~16 KB |
| Matter SDK (Phase 3) | ~100 KB |
| NVS temp buffers | ~2 KB |
| **Free** | **~130–150 KB** |

---

## 11. Testing Strategy

- Host: Core unit tests (C++) without ESP‑IDF
- Target: HAL integration tests + end‑to‑end
- Static analysis: clang‑tidy/cppcheck (embedded-profile config)

---

## 12. Glossary

| Term | Meaning |
|------|---------|
| **HAL** | Hardware Abstraction Layer — wrapper over ESP‑IDF |
| **Effect** | Side effect described by Core and executed by Service/HAL |
| **Snapshot** | Immutable state image (registry/config) |
| **Single Writer** | Single context that creates new snapshots |
| **C ABI** | `extern "C"` boundary for stack compatibility |

---

## 13. Decisions (Pinned)

- <mark>Primary project language: C++ (embedded profile)</mark>
- Immutable Core + Single Writer
- Controlled Side Effects (effects executor)
- C ABI at Zigbee/Matter/ESP‑IDF boundaries
- Memory: snapshot ring/pool, minimum heap on hot paths
