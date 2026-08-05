<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# DeviceId-keyed Matter endpoint identity (S4 required changes #20-#26)

This document records the S4 Matter endpoint contract: what is frozen and
enforced today, and what remains explicitly out of scope for this pass. It
complements `docs/architecture/DEVICE_IDENTITY.md` (S2's `DeviceId`),
`docs/architecture/PERSISTENCE.md` (S3) and `docs/architecture/
GATEWAY_IDENTITY.md` (S4's `GatewayId` slice).

## 1. Problem this piece closes

Before this pass, `matter_bridge` resolved a device's Matter endpoint
through `MatterEndpointMapEntry` (`short_addr -> endpoint`, populated only
by tests -- production code never called `set_endpoint_map()`) with a
fallback to three fixed, class-wide constants (`kMatterEndpointTemperature
= 10`, `kMatterEndpointOccupancy = 11`, `kMatterEndpointContact = 12`).
In production, every device of the same class therefore collided on the
same numeric endpoint -- a real bug, not just an architecture-purity
concern: two temperature sensors could never be distinguished by a Matter
controller. Endpoints also were not persisted anywhere and were keyed by
`short_addr`, which S2/FD-01 established is a mutable locator, never a
durable identity.

## 2. What is implemented and tested

### 2.1 `MatterEndpointRegistry`: DeviceId-keyed, deterministic, persisted

`components/service/include/matter_endpoint_registry.hpp` +
`matter_endpoint_registry.cpp` (new). One endpoint per `core::DeviceId`,
carrying every Matter cluster that device supports (plan #20) -- there is
no per-cluster endpoint concept anymore. Endpoints are drawn from a fixed
range `[kEndpointBase=10, kEndpointRangeEnd=73]`, sized to
`service::kServiceMaxDevices` (64, matching `core::kMaxDevices`), matching
the plan's "dynamic base 10, reviewed range 10-73 for capacity 64" (#21).
`allocate()` assigns the deterministic lowest free slot and is idempotent
for an already-`kAssigned` device_id.

Three states per slot (`kUnassigned`/`kAssigned`/`kPendingRemoval`,
mirroring the enum FD-16 already reserved in
`persisted_device_state.hpp`'s `PersistedMatterEndpointState`) implement
the two-phase removal plan item #24:

- `mark_pending_removal()` -- step 1, transitions `kAssigned ->
  kPendingRemoval`. The slot stops resolving via `find()` (it is being
  retired, not published) but is **not** reusable yet.
- `confirm_removed()` -- step 2, called only once the Matter adapter
  confirms the tombstone (see §2.3). Frees the slot.

`allocate()` deliberately does not treat a `kPendingRemoval` entry for the
*same* `device_id` as reusable either: a device that leaves and rejoins
before its previous endpoint's tombstone is confirmed gets a genuinely new
slot. This extends FD-01's "no state inheritance across rejoin" rule to
Matter identity, and is exactly what the plan's negative test ("address
reuse cannot inherit endpoint") requires.

Tested in `test/host/test_matter_endpoint_registry.cpp`: deterministic
lowest-free assignment; idempotent re-allocation; invalid `DeviceId`
rejected; capacity exhaustion is explicit (`kNoCapacity`, never silently
overwrites); the full two-phase removal sequence, including the
same-device-mid-removal-gets-a-new-slot case; persistence across a
simulated reboot (fresh instance, same underlying NVS); a `kPendingRemoval`
transition surviving a simulated reboot before confirmation (the slot stays
non-reusable, it does not silently revert to unassigned); and a corrupted
store starting empty rather than fabricating a result.

### 2.2 Independent persistence, not piggybacked on S3's per-device record

`PersistedDeviceRecord.matter_endpoint_state`/`matter_endpoint_id`
(`persisted_device_state.hpp`, added in S3 as a structural placeholder)
are **not** used by this implementation. `MatterEndpointRegistry` owns its
own two-generation NVS store (`mtep_a`/`mtep_b` blob keys), replicating
`PersistedStateStore`'s write/validate/commit protocol
(`persisted_state_store.cpp`) rather than reusing that class directly
(which is hardcoded to `PersistedStatePayload`, not generic).

This is a deliberate, necessary deviation from the S3 placeholder's
apparent intent, not an oversight: `core_reducer.cpp`'s `kDeviceLeft`
handling wipes the `CoreDeviceRecord` **immediately** (`device =
CoreDeviceRecord{}`), before the two-phase Matter removal even completes.
A `kPendingRemoval` endpoint must remain non-reusable across that wipe and
across reboot until tombstone confirmation -- once the device has left
Core, there is no `PersistedDeviceRecord` slot left to carry that fact
forward, so it cannot live on the per-device S3 payload. `S3-completion.
json`'s "structural readiness only" fields remain unpopulated dead schema
for now; if a future pass wants to use them (e.g. to avoid a second NVS
key pair), it would need to decouple Matter endpoint lifecycle from
Core device lifecycle in the S3 schema first.

### 2.3 Two-phase removal wiring: `apply_managers()` -> HAL tombstone

`ServiceRuntime::handle_matter_endpoint_removal_on_device_left()`
(`service_runtime.cpp`), called from `apply_managers()` on every
`kDeviceLeft` event (which is the same reducer event as `kDeviceRemoved` --
they are aliases; see `core_events.hpp`): resolves the leaving device's
`DeviceId` (falling back to the locator registry if the event itself
didn't carry one), calls `mark_pending_removal()`, then attempts
`hal_matter_remove_endpoint()` (new HAL primitive, §2.4) and only calls
`confirm_removed()` if that succeeds.

`components/app_hal/include/hal_matter.h` + `hal_matter.c`:
`hal_matter_remove_endpoint(uint16_t endpoint_id)` follows the exact same
weak-hook pattern as the existing `hal_matter_publish_*` functions --
`hal_matter_stack_remove_endpoint()` is a weak symbol defaulting to `-1`
(unavailable) under `ESP_PLATFORM`, overridable by a real Matter stack
backend; the host stub returns `0` (success) for testability, matching
every other `hal_matter_*` host stub.

**Consequence, stated plainly:** until a real Matter adapter is linked on
target, `hal_matter_remove_endpoint()` always fails there, so
`confirm_removed()` is never reached on real hardware today -- endpoints
freed by a leave stay `kPendingRemoval` forever in that configuration. This
is the correct, fail-closed behavior per item #24 ("no reuse before
tombstone confirmation"), not a bug: it is symmetric with `hal_matter_
available()`'s existing truthful-unavailability contract (plan #27,
already correct before this pass and unchanged here) and with the general
principle that this codebase does not fabricate confirmation from an
adapter that was never actually asked. On host builds, where `hal_matter_
remove_endpoint()` truthfully reflects the (fake) host adapter succeeding,
the full two-phase flow completes and is exercised end-to-end by
`test_matter_endpoint_remap_stability.cpp`.

### 2.4 Allocation timing and remap stability

`ServiceRuntime::sync_matter_endpoint_allocations()`, called from
`notify_read_models_from_core_snapshot()` before the Matter-facing read
model is rebuilt (so an allocation is always persisted before the
corresponding snapshot reaches `matter_bridge`, per plan #23): for every
online device with a resolved `DeviceId`, calls `allocate()`. Allocation is
**not** gated on capability discovery (has_temperature/occupancy/contact
being known) -- an endpoint is assigned as soon as identity is resolved,
and simply carries no active clusters until an attribute report arrives.
This is a deliberate simplification over an earlier capability-gated
design: nothing in the plan's test list requires gating, and gating made
allocation timing depend on interview completion order, which is exactly
the kind of timing-dependent behavior this stage is trying to eliminate
elsewhere (S3's persistence protocol, S2's identity resolution).

Because the registry is keyed purely by `DeviceId` and its API has no
`short_addr` parameter anywhere, a locator-only remap (`core_reducer.cpp`'s
`kDeviceJoined` branch: the same `DeviceId` reporting a new `short_addr`
while still online, "INV-ID-01") cannot disturb an existing allocation --
there is no code path through which it could. This satisfies plan #26
("a short-address remap updates locator metadata only and does not emit
device disappearance/recreation") by construction rather than by a
separate check. Tested end-to-end in `test/host/
test_matter_endpoint_remap_stability.cpp`: a device's endpoint is
unchanged after a pure remap; a genuine leave (identity retiring) followed
by a *different* physical device joining does **not** hand the new device
the old device's endpoint by coincidence of timing -- it lands there only
because the retired slot was the sole free one after confirmed removal,
which the DeviceId-keyed `allocate()` call guarantees independent of any
short_addr coincidence.

### 2.5 `matter_bridge` simplification (plan #25)

`components/matter_bridge/`: `MatterEndpointMapEntry`,
`set_endpoint_map()`, `kMatterMaxEndpointMapEntries`, and the entire
`matter_endpoint_map.hpp`/`matter_device_map.cpp` file pair (including the
three fixed class-wide constants) are deleted, not merely deprecated --
nothing production-relevant consumed them once endpoint resolution moved
server-side, and plan #25 explicitly calls for the fixed-class fallback's
removal as production identity behavior. `MatterBridgeDeviceSnapshot`
(`matter_runtime_api.hpp`) gained a plain `uint16_t endpoint` field,
resolved by `BridgeSnapshotBuilder::build_matter_snapshot()`
(`bridge_snapshot_builder.cpp`, a `service`-owned, non-`const`-on-the-write-
path file that may reference `core::DeviceId` freely) via a **read-only**
`MatterEndpointRegistry::find()` lookup -- allocation itself happens
earlier, in §2.4, keeping this builder's `build_matter_snapshot()` `const`
throughout. `matter_bridge.cpp`'s `sync_snapshot()` now uses the
pre-resolved `device.endpoint` directly for every attribute type
(availability, stale, temperature, occupancy, contact) instead of
resolving a separate endpoint per cluster class -- a direct simplification
following from plan #20's "one endpoint carries every cluster."

New architecture rule `INV-H008` locks all of this: `MatterEndpointRegistry`
must exist; `components/matter_bridge` must not reintroduce
`MatterEndpointMapEntry`/`set_endpoint_map`/the fixed-class constants;
`MatterBridgeDeviceSnapshot` must carry a resolved `endpoint` field; and
`hal_matter.h` must expose the removal primitive.

## 3. What is explicitly deferred

### 3.1 Real Matter adapter / target-side tombstone confirmation

No real Matter stack is linked in this repository at any stage prior to
this one, and this pass does not add one. `hal_matter_stack_remove_
endpoint()`'s weak default (`-1`) means `confirm_removed()` is unreachable
on real hardware until a real adapter exists -- see §2.3's "consequence,
stated plainly." This is the same `BLOCKED_TOOLCHAIN`-adjacent limitation
already recorded for every other Matter HAL surface since S0; it does not
block this pass's completion because the plan explicitly accepts "target-
side Matter capability remains false unless a real adapter is linked" (#27)
as correct, not as a gap to close here.

### 3.2 Retry/recovery for a permanently-stuck `kPendingRemoval` slot

Given §3.1, a real-world device leaving on hardware with no Matter
adapter linked leaves its slot `kPendingRemoval` forever (never confirmed,
never freed). No periodic retry, alternate confirmation path, or manual
recovery operation is implemented. Adding one now would be speculative and
unverifiable (there is nothing real to retry against yet); it is a natural
follow-up once a real Matter adapter exists and this failure mode becomes
observable rather than purely theoretical.

### 3.3 HTTP/MQTT/HA exposure of Matter endpoint identity

Nothing in `/api/v1` (not yet built), MQTT v1, or Home Assistant discovery
consumes `MatterEndpointRegistry` yet. Those are separate S4 required-
change groups (HTTP, MQTT, HA) sequenced after GatewayId and this Matter
piece per the user's chosen ordering; this document only covers the
identity/allocation primitive itself.

### 3.4 Capacity/range static-diff review gate (plan #22's `BLOCKED_DELTA_REVIEW`)

Plan item #22 calls for a build-time check that fails with
`BLOCKED_DELTA_REVIEW` if any additional real static/reserved target
endpoint conflicts with the 10-73 range or capacity differs without an
approved plan delta. `INV-H008` locks the *absence* of the old fixed
constants and the *presence* of the new allocator, which covers the
concrete regression this repository could reintroduce, but does not
implement a generic "diff every static endpoint constant in the codebase"
build gate. No other static/reserved Matter endpoint constant exists
anywhere in the repository today (confirmed by repository-wide search
during this pass), so there is currently nothing for such a gate to find;
it is deferred as its own reviewable increment rather than built
speculatively against constants that do not exist.

## 4. Environment limitation

Verified via the Docker host-tools workaround (`zgw-host-tools:s0` for
build/test; `zgw-host-tools:cppcheck` for static analysis). ESP-IDF/`idf.py`
remains unavailable, so `hal_matter.c`'s `ESP_PLATFORM` branches (the weak
hooks themselves, and any real backend that might override them) are
unverified against real hardware -- BLOCKED_TOOLCHAIN, consistent with S0.
