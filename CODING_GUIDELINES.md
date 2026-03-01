<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# CODING_GUIDELINES.md

Zigbee Gateway -- ESP32-C6 -- ESP-IDF 5.5.2 -- C++ (Embedded Profile)

Hardware baseline for target builds:
- ESP32-C6 module with flash size >= 8MB.
- Partition table must include Zigbee persistent storage partitions (`zb_storage`, `zb_fct`).

------------------------------------------------------------------------

# 1. Project Philosophy

The project follows:

-   Immutable Core
-   Single Writer Concurrency Model
-   Event-driven architecture
-   Controlled side effects
-   Explicit memory ownership
-   C ABI boundaries

Core logic must remain platform-agnostic and testable on host.

------------------------------------------------------------------------

# 2. Language Standard

C++17 (embedded-safe subset)

------------------------------------------------------------------------

# 3. Embedded C++ Rules

## 3.1 Forbidden

-   Exceptions (-fno-exceptions)
-   RTTI (-fno-rtti unless explicitly needed)
-   Unbounded dynamic allocation
-   std::vector without fixed capacity control
-   std::string in performance-critical paths
-   Hidden heap growth

## 3.2 Allowed

-   constexpr
-   enum class
-   std::array
-   move semantics
-   RAII for resource ownership
-   placement new
-   fixed-capacity containers

------------------------------------------------------------------------

# 4. Memory Management

## 4.1 General Strategy

  Memory Type   Usage
  ------------- -------------------------------
  Stack         Local temporary objects
  Static        System singletons
  Heap          Only via controlled allocator
  Pool          Snapshots, registry records

## 4.2 Rules

-   No malloc/free in event handlers
-   No new/delete in hot paths
-   All heap usage must go through defined allocator
-   Snapshot objects must use pool or ring buffer
-   Config/NVS data must be versioned and migrated via explicit schema versions

------------------------------------------------------------------------

# 5. Core Layer Rules

-   No ESP-IDF headers
-   No FreeRTOS includes
-   No logging directly (use abstract logger interface)
-   No hardware interaction
-   No global mutable state

Reducer contract:

```cpp
struct CoreReduceResult {
    CoreState next;
    CoreEffectList effects;
};

CoreReduceResult core_reduce(const CoreState& prev, const CoreEvent& ev) noexcept;
```

Reducer must be pure (no side effects) and describe intents only via `effects`.

------------------------------------------------------------------------

# 6. Service Layer Rules

-   Responsible for side effects
-   Executes Effects returned by Core
-   Owns Single Writer task
-   Performs atomic snapshot swap

------------------------------------------------------------------------

# 7. HAL Layer Rules

-   Thin wrappers over ESP-IDF
-   No business logic
-   Must be mockable for host tests
-   Expose C-compatible API

Example:

``` cpp
extern "C" void zigbee_callback(...);
```

------------------------------------------------------------------------

# 8. Concurrency Model

-   One writer task modifies state
-   Readers access immutable snapshots
-   No RWMutex in Core
-   Short critical sections only in HAL or dispatcher

------------------------------------------------------------------------

# 9. Error Handling

-   Use esp_err_t at system boundaries
-   Use enum class for domain errors
-   No exceptions
-   All errors must be explicit

------------------------------------------------------------------------

# 10. Logging

-   Use centralized log tags
-   No logging inside tight loops
-   Logging disabled or reduced in Release

------------------------------------------------------------------------

# 11. Naming Conventions

Types: PascalCase Functions: snake_case Members: trailing underscore
(member\_) Constants: UPPER_CASE Namespaces: lowercase

------------------------------------------------------------------------

# 12. Testing

## Host Tests

-   Pure Core logic
-   No ESP-IDF dependencies
-   Deterministic

## Target Tests

-   HAL integration
-   Zigbee command execution
-   NVS migration

------------------------------------------------------------------------

# 13. Static Analysis

Recommended:

-   clang-tidy
-   cppcheck
-   -Wall -Wextra -Werror

------------------------------------------------------------------------

# 14. Build Flags

``` cmake
-fno-exceptions
-fno-rtti
-fno-threadsafe-statics
-Wall
-Wextra
-Werror
```

Release:

``` cmake
-O2
-flto
```

------------------------------------------------------------------------

# 15. Long-Term Stability Guarantees

This architecture guarantees:

-   Deterministic concurrency
-   No hidden mutable state
-   Matter-ready integration
-   Predictable memory usage
-   High testability
