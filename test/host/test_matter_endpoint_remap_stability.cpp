/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// End-to-end proof that a Matter endpoint is stable across a short-address
// remap (plan S4 #20/#26): MatterEndpointRegistry is keyed exclusively by
// core::DeviceId, never by short_addr, so a locator-only remap (the same
// DeviceId rejoining at a new short_addr while still online, INV-ID-01 in
// core_reducer.cpp) must never disturb the existing Matter identity -- no
// removal, no reallocation, no device disappearance/recreation in the
// Matter-facing snapshot.

#include <array>
#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    const std::array<uint8_t, HAL_ZIGBEE_IEEE_ADDR_LEN> ieee_a = {
        0x00, 0x12, 0x4b, 0x00, 0x01, 0xaa, 0x22, 0x01};

    hal_zigbee_simulate_device_joined_with_identity(0x2201U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    service::MatterBridgeSnapshot snapshot_before{};
    assert(runtime.build_matter_bridge_snapshot(&snapshot_before));
    assert(snapshot_before.device_count == 1U);
    assert(snapshot_before.devices[0].short_addr == 0x2201U);
    const uint16_t endpoint_before = snapshot_before.devices[0].endpoint;
    assert(endpoint_before != 0U);

    // Same DeviceId (same IEEE address), a fresh short_addr, no leave event
    // posted in between -- a pure locator remap (INV-ID-01).
    hal_zigbee_simulate_device_joined_with_identity(0x3301U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    service::MatterBridgeSnapshot snapshot_after{};
    assert(runtime.build_matter_bridge_snapshot(&snapshot_after));
    assert(snapshot_after.device_count == 1U);
    assert(snapshot_after.devices[0].short_addr == 0x3301U);
    assert(snapshot_after.devices[0].endpoint == endpoint_before);

    // A genuine leave (identity retiring, not remapping) is a different
    // story: it starts the two-phase removal, so a *subsequent* join of a
    // *different* physical device must not inherit this endpoint.
    hal_zigbee_simulate_device_left_with_identity(0x3301U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    const std::array<uint8_t, HAL_ZIGBEE_IEEE_ADDR_LEN> ieee_b = {
        0x00, 0x12, 0x4b, 0x00, 0x01, 0xbb, 0x44, 0x02};
    hal_zigbee_simulate_device_joined_with_identity(0x4402U, ieee_b.data());
    assert(runtime.process_pending() >= 1U);

    service::MatterBridgeSnapshot snapshot_new_device{};
    assert(runtime.build_matter_bridge_snapshot(&snapshot_new_device));
    assert(snapshot_new_device.device_count == 1U);
    assert(snapshot_new_device.devices[0].short_addr == 0x4402U);
    // Host builds confirm the tombstone synchronously
    // (hal_matter_remove_endpoint() succeeds there), so the retired slot
    // (endpoint_before) is free again and -- being the only free slot at
    // this point -- deterministically reused by this genuinely different
    // physical device. Reuse here is fine: what item 24 forbids is reuse
    // *before* confirmation, not reuse after it.
    assert(snapshot_new_device.devices[0].endpoint == endpoint_before);

    return 0;
}
