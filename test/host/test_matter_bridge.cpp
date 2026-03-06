/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "matter_bridge.hpp"

namespace {

core::CoreState make_state(uint16_t short_addr,
                           bool online,
                           bool has_temperature,
                           int16_t temperature_centi_c,
                           core::CoreOccupancyState occupancy,
                           core::CoreContactState contact,
                           bool stale) {
    core::CoreState state{};
    state.device_count = online ? 1U : 0U;
    state.devices[0].short_addr = short_addr;
    state.devices[0].online = online;
    state.devices[0].has_temperature = has_temperature;
    state.devices[0].temperature_centi_c = temperature_centi_c;
    state.devices[0].occupancy_state = occupancy;
    state.devices[0].contact_state = contact;
    state.devices[0].stale = stale;
    return state;
}

bool has_update(const matter_bridge::MatterAttributeUpdate* updates,
                std::size_t count,
                matter_bridge::MatterAttributeType type,
                uint16_t endpoint) {
    for (std::size_t i = 0; i < count; ++i) {
        if (updates[i].type == type && updates[i].endpoint == endpoint) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using namespace matter_bridge;

    MatterBridge bridge;

    const core::CoreState first = make_state(
        0x2201U,
        true,
        true,
        2150,
        core::CoreOccupancyState::kOccupied,
        core::CoreContactState::kClosed,
        false);

    // Not started => no updates.
    assert(bridge.sync_snapshot(first) == 0U);

    MatterEndpointMapEntry map[] = {{0x2201U, 50U}};
    assert(bridge.set_endpoint_map(map, 1U));

    assert(bridge.start());
    assert(bridge.started());

    const std::size_t first_count = bridge.sync_snapshot(first);
    assert(first_count == 5U);

    MatterAttributeUpdate out[kMatterMaxUpdatesPerSync]{};
    const std::size_t drained_first = bridge.drain_attribute_updates(out, kMatterMaxUpdatesPerSync);
    assert(drained_first == first_count);
    assert(has_update(out, drained_first, MatterAttributeType::kAvailabilityOnline, 50U));
    assert(has_update(out, drained_first, MatterAttributeType::kStale, 50U));
    assert(has_update(out, drained_first, MatterAttributeType::kTemperatureCentiC, 50U));
    assert(has_update(out, drained_first, MatterAttributeType::kOccupancy, 50U));
    assert(has_update(out, drained_first, MatterAttributeType::kContactOpen, 50U));

    // Same snapshot => no deltas.
    assert(bridge.sync_snapshot(first) == 0U);
    assert(bridge.drain_attribute_updates(out, kMatterMaxUpdatesPerSync) == 0U);

    core::CoreState changed = first;
    changed.devices[0].temperature_centi_c = 2200;
    changed.devices[0].contact_state = core::CoreContactState::kOpen;
    changed.devices[0].stale = true;
    const std::size_t changed_count = bridge.sync_snapshot(changed);
    assert(changed_count == 3U);
    const std::size_t drained_changed = bridge.drain_attribute_updates(out, kMatterMaxUpdatesPerSync);
    assert(drained_changed == changed_count);
    assert(has_update(out, drained_changed, MatterAttributeType::kTemperatureCentiC, 50U));
    assert(has_update(out, drained_changed, MatterAttributeType::kContactOpen, 50U));
    assert(has_update(out, drained_changed, MatterAttributeType::kStale, 50U));

    // Device removed => offline availability.
    core::CoreState removed{};
    removed.device_count = 0U;
    const std::size_t removed_count = bridge.sync_snapshot(removed);
    assert(removed_count == 1U);
    const std::size_t drained_removed = bridge.drain_attribute_updates(out, kMatterMaxUpdatesPerSync);
    assert(drained_removed == 1U);
    assert(out[0].type == MatterAttributeType::kAvailabilityOnline);
    assert(out[0].endpoint == 50U);
    assert(out[0].bool_value == false);

    bridge.stop();
    assert(!bridge.started());
    assert(bridge.sync_snapshot(first) == 0U);

    return 0;
}
