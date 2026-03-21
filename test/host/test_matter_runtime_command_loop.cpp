/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "matter_bridge.hpp"
#include "service_runtime.hpp"

namespace {

bool has_bool_update(const matter_bridge::MatterAttributeUpdate* updates,
                     std::size_t count,
                     matter_bridge::MatterAttributeType type,
                     uint16_t endpoint,
                     bool value) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (updates[i].type == type && updates[i].endpoint == endpoint && updates[i].bool_value == value) {
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

    matter_bridge::MatterAttributeUpdate updates[matter_bridge::kMatterMaxUpdatesPerSync]{};
    assert(bridge.sync_runtime_snapshot() == 2U);
    std::size_t drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 2U);
    assert(has_bool_update(updates, drained, matter_bridge::MatterAttributeType::kAvailabilityOnline, 50U, true));
    assert(has_bool_update(updates, drained, matter_bridge::MatterAttributeType::kStale, 50U, false));

    uint32_t correlation_id = 0U;
    uint32_t correlation_id_2 = 0U;
    assert(bridge.post_power_command(0x2201U, true, 100U, &correlation_id) == service::CommandSubmitStatus::kAccepted);
    assert(
        bridge.post_power_command(0x2201U, false, 101U, &correlation_id_2) ==
        service::CommandSubmitStatus::kAccepted);
    assert(correlation_id != 0U);
    assert(correlation_id_2 != 0U);
    assert(correlation_id_2 > correlation_id);
    assert(runtime.process_pending() >= 1U);
    assert(runtime.pending_commands() == 2U);

    // Power command ingress itself must not emit Matter deltas until snapshot payload changes.
    assert(bridge.sync_runtime_snapshot() == 0U);
    assert(bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync) == 0U);

    core::CoreCommandResult result{};
    result.correlation_id = correlation_id;
    result.type = core::CoreCommandResultType::kSuccess;
    result.completed_at_ms = 110U;
    assert(runtime.handle_command_result(result) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 1U);

    core::CoreCommandResult result_2{};
    result_2.correlation_id = correlation_id_2;
    result_2.type = core::CoreCommandResultType::kSuccess;
    result_2.completed_at_ms = 111U;
    assert(runtime.handle_command_result(result_2) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 0U);
    assert(bridge.sync_runtime_snapshot() == 0U);
    assert(bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync) == 0U);

    core::CoreEvent occupancy{};
    occupancy.type = core::CoreEventType::kDeviceTelemetryUpdated;
    occupancy.device_short_addr = 0x2201U;
    occupancy.value_u32 = 120U;
    occupancy.telemetry_kind = core::CoreTelemetryKind::kOccupancy;
    occupancy.telemetry_i32 = 1;
    occupancy.telemetry_valid = true;
    assert(runtime.post_event(occupancy));
    assert(runtime.process_pending() == 1U);

    assert(bridge.sync_runtime_snapshot() == 1U);
    drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 1U);
    assert(has_bool_update(updates, drained, matter_bridge::MatterAttributeType::kOccupancy, 50U, true));

    occupancy.value_u32 = 130U;
    occupancy.telemetry_i32 = 0;
    assert(runtime.post_event(occupancy));
    assert(runtime.process_pending() == 1U);

    assert(bridge.sync_runtime_snapshot() == 1U);
    drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 1U);
    assert(has_bool_update(updates, drained, matter_bridge::MatterAttributeType::kOccupancy, 50U, false));

    bridge.stop();
    return 0;
}
