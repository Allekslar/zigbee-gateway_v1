/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "matter_bridge.hpp"
#include "service_runtime.hpp"

namespace {

bool has_update(const matter_bridge::MatterAttributeUpdate* updates,
                std::size_t count,
                matter_bridge::MatterAttributeType type,
                uint16_t endpoint,
                bool bool_value) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (updates[i].type == type && updates[i].endpoint == endpoint && updates[i].bool_value == bool_value) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    matter_bridge::MatterBridge bridge;
    bridge.attach_runtime(&runtime);
    assert(bridge.start());

    // A resolved DeviceId is required for MatterEndpointRegistry to
    // allocate an endpoint (plan S4 #20).
    const std::array<uint8_t, HAL_ZIGBEE_IEEE_ADDR_LEN> ieee_a = {
        0x00, 0x12, 0x4b, 0x00, 0x01, 0xaa, 0x22, 0x01};
    hal_zigbee_simulate_device_joined_with_identity(0x2201U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    const std::size_t joined_count = bridge.sync_runtime_snapshot();
    assert(joined_count == 2U);
    matter_bridge::MatterAttributeUpdate updates[matter_bridge::kMatterMaxUpdatesPerSync]{};
    std::size_t drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == joined_count);
    assert(has_update(
        updates, drained, matter_bridge::MatterAttributeType::kAvailabilityOnline,
        service::MatterEndpointRegistry::kEndpointBase, true));
    assert(has_update(
        updates, drained, matter_bridge::MatterAttributeType::kStale,
        service::MatterEndpointRegistry::kEndpointBase, false));

    assert(bridge.sync_runtime_snapshot() == 0U);

    hal_zigbee_simulate_device_left_with_identity(0x2201U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    const std::size_t left_count = bridge.sync_runtime_snapshot();
    assert(left_count == 1U);
    drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 1U);
    assert(has_update(
        updates, drained, matter_bridge::MatterAttributeType::kAvailabilityOnline,
        service::MatterEndpointRegistry::kEndpointBase, false));

    bridge.stop();
    return 0;
}
