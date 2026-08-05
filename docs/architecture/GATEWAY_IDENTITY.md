<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Canonical GatewayId (S4 slice of FD-17)

This document records the S4 GatewayId contract: what is frozen and
enforced today, and what remains explicitly out of scope for this stage.
It is the canonical reference for the `GatewayId` portion of plan FD-17,
named by `docs/implementation/PRODUCTION_HARDENING_PLAN.md` Stage S4, and
complements `docs/architecture/DEVICE_IDENTITY.md` (S2's `DeviceId`) and
`docs/architecture/PERSISTENCE.md` (S3).

## 1. Problem this piece closes

Before this pass, the firmware had no gateway-level identity concept at
all. MQTT payloads, Home Assistant discovery entities and (eventually)
HTTP `/api/v1` responses all need one stable, hardware-bound identifier for
*the gateway itself*, distinct from `core::DeviceId` (which identifies a
*paired Zigbee device*). Per FD-17, that identifier must survive ordinary
and factory reset and must never be substituted by anything mutable
(hostname, Wi-Fi MAC, Zigbee coordinator address) — a config change or
network re-provision must not silently change what MQTT/HA callers see as
"the gateway."

## 2. What is implemented and tested

### 2.1 Layering: pure formatting vs. HAL-owned hardware read

Mirroring the `core::DeviceId` / HAL-boundary split established in S2:

- `components/common/include/gateway_id.hpp` + `gateway_id.cpp` define
  `common::GatewayId` — a trivial value type wrapping 6 bytes, with
  `format()` (exactly 12 lowercase hex characters, no separators),
  `valid()` (rejects all-zero, i.e. an unprogrammed eFuse block), and
  value semantics (`operator==`/`!=`, copyable). This file has **no**
  ESP-IDF dependency, enforced by architecture rule `INV-H007`.
- `components/app_hal/include/hal_identity.h` + `hal_identity.c` own the
  actual hardware read: `hal_identity_get_factory_base_mac()` calls
  `esp_efuse_mac_get_default()` under `ESP_PLATFORM` (the factory base MAC
  from eFuse BLK0 — deliberately not `esp_wifi_get_mac()` or any
  interface-specific MAC, which are mutable/interface-dependent). `INV-H007`
  also asserts this is the only file allowed to reference
  `esp_efuse_mac_get_default`.

`common::GatewayId` differs from `core::DeviceId` in one deliberate way:
unlike `DeviceId`, which the architecture keeps opaque to `web_ui`/
`mqtt_bridge` adapters (`INV-M026`/`INV-M030` forbid `core::` symbols
there), `GatewayId` lives in `common`, which those adapters may reference
directly — it is a transport-facing identity by design (every MQTT/HA/HTTP
payload will carry it), not a Core domain key.

### 2.2 Resolution and caching in ServiceRuntime

`ServiceRuntime::gateway_id_` is resolved exactly once, in the
constructor, via a `resolve_gateway_id()` helper that calls
`hal_identity_get_factory_base_mac()` and returns a default-constructed
(invalid) `GatewayId` on HAL failure rather than fabricating one. The
value is immutable for the process lifetime — no lock is needed for reads,
unlike `capabilities_` which is externally mutable via `set_capabilities()`.
Exposed through `ServiceRuntimeApi::gateway_id() const noexcept`, so every
call site (Web/MQTT/HA handlers, once wired in a later S4 pass) reaches it
through the same interface already used for `capabilities()`.

Tested in `test/host/test_service_gateway_id.cpp`: a mock factory base MAC
(via `hal_identity_test.h`, gated by `SERVICE_RUNTIME_TEST_HOOKS` like
`hal_zigbee_test.h`) resolves to the expected 12-hex-character `GatewayId`;
the same MAC across a freshly constructed `ServiceRuntime` (simulating
reboot: the underlying eFuse value does not change) yields an identical
`GatewayId`; two distinct MACs yield two distinct `GatewayId`s; and the
value is reachable through `ServiceRuntimeApi&`, not only the concrete
class.

`test/host/test_common_gateway_id.cpp` covers the pure value type in
isolation: format round trip, byte-order preservation, equality,
undersized-buffer rejection, and default-constructed invalidity.

### 2.3 Anti-spoofing on the mock override

`hal_identity_set_mock_base_mac()` (declared in `hal_identity_test.h`,
defined unconditionally in `hal_identity.c` to mirror the existing
`hal_zigbee.c` pattern) is a no-op when compiled with `ESP_PLATFORM`
defined. Even if a target test binary accidentally linked the test-only
header, it could not override the real eFuse-derived identity — the
production code path is the only one that can ever produce a `GatewayId`
on real hardware.

## 3. What is explicitly deferred

### 3.1 Everything else in FD-17

FD-17 is much larger than `GatewayId` alone: production mDNS host naming
(`zigbee-gateway-<last6>.local`), the management TLS certificate
(SAN/issuer/CA chain, current/next encrypted slots, authenticated rotation
with physical presence), out-of-band CA trust provisioning, and
manufacturing/provisioning duplicate-enrollment rejection. None of that is
implemented here. Per the plan's own stage ownership (`FD-19`), **S6 is the
stage that wires TLS trust, certificate rotation and the production
management composition root** — this pass only produces the one frozen
building block (`GatewayId`) that S4's MQTT/HA/HTTP work and S6's mDNS/SAN
work will both consume.

### 3.2 Consumers (MQTT `gateway_id` field, HA identifiers, HTTP DTOs)

`ServiceRuntimeApi::gateway_id()` exists and is tested in isolation, but
nothing in `mqtt_bridge`, the (not-yet-existing) `/api/v1` HTTP layer, or
Home Assistant discovery payloads calls it yet. Wiring those consumers is
scoped to the MQTT v1 / HA discovery migration / HTTP v1 slices of S4,
which come after this foundational piece per the agreed S4 sequencing.

### 3.3 Persistence

`GatewayId` is not written to NVS or any persisted schema. It does not
need to be: it is re-derived from the eFuse factory base MAC on every
boot, which is itself the durable store (outside application code's
control, per FD-17: "never derived from mutable network configuration").
There is nothing for S3's persistence layer to own here.

## 4. Environment limitation

Verified via the Docker host-tools workaround (`zgw-host-tools:s0` for
build/test, plus a locally built `zgw-host-tools:cppcheck` derivative for
static analysis — cppcheck is not part of the base image and was installed
into a separate tagged image for this verification pass). ESP-IDF/`idf.py`
remains unavailable, so the `ESP_PLATFORM` branch of
`hal_identity_get_factory_base_mac()` (the actual `esp_efuse_mac_get_
default()` call) is unverified against real hardware — consistent with the
BLOCKED_TOOLCHAIN status S0 recorded for all target/HIL code. The
`REQUIRES esp_hw_support` addition to `components/app_hal/CMakeLists.txt`
(needed for `esp_mac.h`) is likewise unverified by a real ESP-IDF
component-registration pass.
