/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "matter_bridge.hpp"

namespace {

service::MatterBridgeSnapshot make_snapshot(uint32_t revision,
                                            uint16_t short_addr,
                                            bool online,
                                            bool stale,
                                            bool has_temperature,
                                            int16_t temperature_centi_c,
                                            bool has_occupancy,
                                            bool occupied) {
    service::MatterBridgeSnapshot snapshot{};
    snapshot.revision = revision;
    snapshot.device_count = 1U;
    snapshot.devices[0].short_addr = short_addr;
    snapshot.devices[0].online = online;
    snapshot.devices[0].stale = stale;
    snapshot.devices[0].primary_class = service::MatterBridgeDeviceClass::kTemperature;
    snapshot.devices[0].has_temperature = has_temperature;
    snapshot.devices[0].temperature_centi_c = temperature_centi_c;
    snapshot.devices[0].has_occupancy = has_occupancy;
    snapshot.devices[0].occupied = occupied;
    snapshot.devices[0].has_contact = false;
    snapshot.devices[0].contact_open = false;
    return snapshot;
}

bool has_bool_update(const matter_bridge::MatterAttributeUpdate* updates,
                     std::size_t count,
                     matter_bridge::MatterAttributeType type,
                     uint16_t endpoint,
                     bool value) {
    for (std::size_t i = 0; i < count; ++i) {
        if (updates[i].type == type && updates[i].endpoint == endpoint && updates[i].bool_value == value) {
            return true;
        }
    }
    return false;
}

bool has_int_update(const matter_bridge::MatterAttributeUpdate* updates,
                    std::size_t count,
                    matter_bridge::MatterAttributeType type,
                    uint16_t endpoint,
                    int32_t value) {
    for (std::size_t i = 0; i < count; ++i) {
        if (updates[i].type == type && updates[i].endpoint == endpoint && updates[i].int_value == value) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using matter_bridge::MatterAttributeType;
    using matter_bridge::MatterAttributeUpdate;
    using matter_bridge::MatterBridge;
    using matter_bridge::MatterEndpointMapEntry;

    MatterBridge bridge;
    const MatterEndpointMapEntry map[] = {
        {0x2201U, 50U},
    };
    assert(bridge.set_endpoint_map(map, 1U));
    assert(bridge.start());

    MatterAttributeUpdate updates[matter_bridge::kMatterMaxUpdatesPerSync]{};

    // Initial active snapshot must publish availability + stale + telemetry fields.
    const service::MatterBridgeSnapshot first = make_snapshot(
        1U, 0x2201U, true, false, true, 2150, true, true);
    assert(bridge.sync_snapshot(first) == 4U);
    std::size_t drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 4U);
    assert(has_bool_update(updates, drained, MatterAttributeType::kAvailabilityOnline, 50U, true));
    assert(has_bool_update(updates, drained, MatterAttributeType::kStale, 50U, false));
    assert(has_int_update(updates, drained, MatterAttributeType::kTemperatureCentiC, 50U, 2150));
    assert(has_bool_update(updates, drained, MatterAttributeType::kOccupancy, 50U, true));

    // Revision-only changes must not produce deltas when payload is unchanged.
    service::MatterBridgeSnapshot same_payload = first;
    same_payload.revision = 2U;
    assert(bridge.sync_snapshot(same_payload) == 0U);
    assert(bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync) == 0U);

    // Stale transition must emit stale-only update.
    service::MatterBridgeSnapshot stale_changed = same_payload;
    stale_changed.revision = 3U;
    stale_changed.devices[0].stale = true;
    assert(bridge.sync_snapshot(stale_changed) == 1U);
    drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 1U);
    assert(has_bool_update(updates, drained, MatterAttributeType::kStale, 50U, true));

    // Removing the active device must emit offline availability update.
    service::MatterBridgeSnapshot removed{};
    removed.revision = 4U;
    removed.device_count = 0U;
    assert(bridge.sync_snapshot(removed) == 1U);
    drained = bridge.drain_attribute_updates(updates, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(drained == 1U);
    assert(has_bool_update(updates, drained, MatterAttributeType::kAvailabilityOnline, 50U, false));

    bridge.stop();
    return 0;
}
