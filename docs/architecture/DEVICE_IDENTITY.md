<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Device identity (S2)

This document records the S2 device identity contract: what is frozen and
enforced today, and what remains open. It is the canonical reference named by
`docs/implementation/PRODUCTION_HARDENING_PLAN.md` Stage S2.

## 1. Canonical identity (FD-01)

- `core::DeviceId` (`components/core/include/device_id.hpp`) is the durable
  device identity: a Zigbee EUI-64, stored as 8 bytes in canonical
  most-significant-byte-first order. Canonical text form is exactly 16
  lowercase hex characters, no separators. All-zero and all-`0xff` values are
  invalid by construction (`DeviceId::valid()`).
- `short_addr` is a mutable network locator. It is never, on its own, a
  durable key for Core state, the descriptor store or the locator registry.

## 2. What is implemented and tested (this stage)

### 2.1 `core::DeviceId`

Trivial, fixed-size, no heap allocation. `parse()`/`format()` round-trip the
canonical hex form; `valid()` rejects all-zero/all-`0xff`. Comparison and a
bounded lexicographic `operator<` are provided (no `std::hash` dependency).
Tested in `test/host/test_core_device_id.cpp`.

### 2.2 `service::DeviceLocatorRegistry`

`components/service/include/device_locator_registry.hpp` / `.cpp`. Bounded
(`kServiceMaxDevices` entries), bidirectional `DeviceId <-> short_addr`
mapping with a monotonically increasing `mapping_revision` per entry.
`remap()` implements steps 1-5 of the plan's Section 7.2 remap algorithm:

1. validates `device_id`/`short_addr`;
2. the old reverse mapping for this `DeviceId` is implicitly released (the
   entry's `short_addr` field is simply overwritten -- there is only one
   array, not two separately-maintained indexes to reconcile);
3. detects whether the requested `short_addr` currently belongs to a
   different, still-online `DeviceId`;
4. marks that displaced device's locator **offline** without transferring or
   erasing its config/state (INV-ID-03); its stale `short_addr` is kept only
   as a diagnostic breadcrumb and is never matched again while offline;
5. installs the new bidirectional mapping and returns a fresh revision.

Steps 6-8 of the plan's algorithm (emit one domain event, persist, schedule
integration resync) are the **caller's** responsibility once this registry is
wired into `ServiceRuntime` -- see §3.1.

Tested in `test/host/test_device_locator_registry.cpp`, including the
INV-ID-01/02/03 scenarios (same-id relocation, displacement, capacity
boundary, invalid-argument non-mutation).

### 2.3 Core reducer: DeviceId-primary matching with a legacy fallback

`components/core/include/core_state.hpp` / `core_events.hpp` /
`core_commands.hpp` now declare a `DeviceId device_id` field on
`CoreDeviceRecord`, `CoreEvent` and `CoreCommand` respectively (enforced by
architecture rule `INV-H005`). `components/core/core_reducer.cpp`'s
`find_device_index()` resolves a device record as follows:

- **If the event's `device_id` is valid**, it is the *only* key consulted. A
  record that owns a resolved identity can never be matched, mutated or
  displaced by a short_addr-only event, so INV-ID-03 holds unconditionally
  for any device once it has a real identity.
- **If `device_id` is invalid**, matching falls back to `short_addr`, but
  only against *other* identity-less records. A legacy, unresolved-identity
  event can therefore never touch a record that already has a resolved
  identity.

This is a deliberate transitional design, analogous in spirit to the plan's
own "dual-field" guidance for bridge/API snapshots (Section 9, S2 required
change #14), extended to the Core/Service boundary for this stage. It exists
because the Service-layer call sites that construct `CoreEvent`s (HAL join
callback, interview/bind/reporting-configured completions, raw attribute
report ingestion -- see §3) do not yet resolve a `DeviceId` before posting
into Core. Rejecting every such event outright (the plan's literal target
end-state) would have silently broken every device-lifecycle test and, more
importantly, real device join/report handling, with no way to verify the fix
in this environment (see §4).

`kDeviceJoined` additionally distinguishes a **locator refresh** (the same
`DeviceId` reports a new `short_addr` while already online -- INV-ID-01,
short-address remap without an intervening leave) from a **first join**
(which alone triggers the `kZigbeeInterview` effect). A locator refresh does
not restart the interview/reporting lifecycle.

New coverage proving the DeviceId-primary path (not just that nothing broke):
`test/host/test_core_reducer_device_id.cpp`. It exercises, with real
`DeviceId` values:

- join creates one record and triggers interview;
- the same `DeviceId` rejoining at a new `short_addr` while online updates
  the locator only -- one device, no second interview;
- a different `DeviceId` claiming the `short_addr` the first device just
  vacated creates a wholly separate record and inherits nothing;
- a legacy (unresolved-identity) event can never mutate a resolved record
  even when it shares that record's current `short_addr`;
- duplicate joins are idempotent (no revision bump, no effects);
- `kDeviceLeft` by `DeviceId` frees the record.

Existing behavior is unchanged for every call site that has not yet been
updated to supply a `DeviceId`: 73/73 host tests, 7/7 integration tests and
73/73 tests under ASan+UBSan all pass unmodified from the pre-S2 baseline.

### 2.4 `DeviceIdentityStore` renamed to `DeviceDescriptorStore`

Per plan S2 required change #11 ("do not overload 'identity' to mean
descriptor"): the store that tracks Zigbee-interview-resolved
manufacturer/model strings has been renamed
`DeviceIdentityStore` -> `DeviceDescriptorStore` (and
`DeviceIdentityEntry`/`DeviceIdentityStatus` -> `DeviceDescriptorEntry`/
`DeviceDescriptorStatus`), so the word "identity" in this codebase refers
exclusively to `core::DeviceId`. This is a pure rename of internal C++
symbols and file names; it does **not** change the external HTTP contract --
the JSON field `"identity_status"` in `/api/devices` responses is untouched
(external contract changes belong to Stage S4). `DeviceDescriptorStore`
itself is still keyed by `short_addr`; re-keying it by `DeviceId` depends on
the same Service-layer wiring gap described in §3.

## 3. What is explicitly deferred

### 3.1 Service-layer identity resolution wiring

`DeviceLocatorRegistry` exists and is tested standalone but is **not yet
wired into `ServiceRuntime`**. The full chain that would make it load-bearing
in production spans multiple layers discovered during this stage:

```
HAL join/leave signal (real ESP-Zigbee stack or host mock)
  -> hal_zigbee_callbacks_t (on_device_joined/on_device_left: short_addr only today)
  -> hal_event_adapter.cpp (zigbee_device_joined_cb/zigbee_device_left_cb)
  -> ServiceRuntime::post_zigbee_join_candidate() -> ZigbeeLifecycleCoordinator::handle_join_candidate()
  -> core::CoreEvent{kDeviceJoined, ...} posted into Core
```

and separately:

```
ServiceRuntime::post_zigbee_interview_result() / post_zigbee_bind_result() /
post_zigbee_configure_reporting_result()
  -> core::CoreEvent{kDeviceInterviewCompleted | kDeviceBindingReady | kDeviceReportingConfigured, ...}
```

None of these currently populate `device_id`. Wiring them requires:

1. Extending `hal_zigbee_callbacks_t::on_device_joined`/`on_device_left`
   (`components/app_hal/include/hal_zigbee.h`) to carry an EUI-64.
2. On the real ESP-Zigbee (`ESP_PLATFORM`) path: `components/app_hal/hal_zigbee.c`
   already maintains an internal IEEE-address table
   (`s_known_device_identities`, `upsert_known_device_identity()`,
   `find_known_device_by_ieee()`) and already detects short-address remaps by
   IEEE address (`notify_join_with_identity()`) -- today it resolves this
   only to synthesize a `LEAVE` + `JOIN` callback pair for the *old* API
   shape. Per plan S2 required change #7, this should become a single
   identity-aware callback instead of that synthetic leave/join pair, once
   the callback struct carries EUI-64.
3. On the host mock path (`hal_zigbee_simulate_device_joined()` and
   friends): extending the mock signatures to accept a test-supplied EUI-64.
4. `hal_event_adapter.cpp` resolving the EUI-64 through
   `DeviceLocatorRegistry` and setting `CoreEvent::device_id` before posting.
5. Threading the resolved `DeviceId` through
   `ZigbeeLifecycleCoordinator::handle_join_candidate()` and the three
   `post_zigbee_*_result()` methods listed above.

Item 2 touches real `esp_zb_*` SDK callback code that cannot be compiled or
verified in this environment (no ESP-IDF/esp-zigbee-lib toolchain available;
see `implementation-evidence/S0-baseline.json`). Items 1, 3 and 4 are
host-buildable and verifiable but were not attempted in this pass once the
true depth of the chain (discovered while investigating item 2's
prerequisites) made clear that doing this correctly, for every event type,
was a substantially larger and separately-reviewable unit of work than the
`DeviceId`/`DeviceLocatorRegistry`/reducer foundation delivered in this
stage.

**Consequence:** the real running system does not yet close the
short-address-reuse identity-confusion vulnerability end to end. `Core` and
`DeviceLocatorRegistry` are ready and correct; the Service/HAL plumbing that
would make every device event carry a resolved `DeviceId` in production is
the next concrete increment.

### 3.2 `NetworkGenerationId` and `LegacyIdentityEvidenceSnapshot` (FD-15)

Not implemented this stage. These depend on the same evidence-capture
machinery the plan schedules immediately before S3 migration (join windows,
network mutations and external command ingress disabled during capture) and
are naturally sequenced with the S3 persistence work, not the S2 identity
foundation.

### 3.3 Matter/MQTT bridge and Web/API identity exposure

Bridge snapshot structures, HTTP DTOs and MQTT topics remain `short_addr`-
keyed, per plan Section 7.2's explicit guidance to keep these dual-field only
starting in S3/S4 ("so S3/S4 can migrate without parallel incompatible
edits"). Untouched in this stage.

## 4. Environment limitation

All verification in this stage ran via the Docker host-tools workaround
established in S0 (`zgw-host-tools:s0`, Ubuntu 24.04 + build-essential +
cmake + clang). ESP-IDF/`idf.py` remains unavailable, so:

- `main/app_main.cpp` and any `ESP_PLATFORM`-guarded code in
  `components/app_hal/hal_zigbee.c` are not compiler-verified;
- no target build or HIL was attempted.

Everything reported as passing in this document was actually built and
executed (host + integration + ASan/UBSan) in this session; nothing is
claimed without having been run.
