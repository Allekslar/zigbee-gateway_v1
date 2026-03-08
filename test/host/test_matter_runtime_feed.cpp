/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
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

    matter_bridge::MatterBridge bridge;
    const matter_bridge::MatterEndpointMapEntry map[] = {{0x2201U, 50U}};
    assert(bridge.set_endpoint_map(map, 1U));
    bridge.attach_runtime(&runtime);
    assert(bridge.start());

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);

    const std::size_t joined_count = bridge.sync_runtime_snapshot();
    assert(joined_count == 2U);
    matter_bridge::MatterAttributeUpdate updates[matter_bridge::kMatterMaxUpdatesPerSync]{};
    std::size_t drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == joined_count);
    assert(has_update(updates, drained, matter_bridge::MatterAttributeType::kAvailabilityOnline, 50U, true));
    assert(has_update(updates, drained, matter_bridge::MatterAttributeType::kStale, 50U, false));

    assert(bridge.sync_runtime_snapshot() == 0U);

    core::CoreEvent left{};
    left.type = core::CoreEventType::kDeviceLeft;
    left.device_short_addr = 0x2201U;
    assert(runtime.post_event(left));
    assert(runtime.process_pending() == 1U);

    const std::size_t left_count = bridge.sync_runtime_snapshot();
    assert(left_count == 1U);
    drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 1U);
    assert(has_update(updates, drained, matter_bridge::MatterAttributeType::kAvailabilityOnline, 50U, false));

    bridge.stop();
    return 0;
}
