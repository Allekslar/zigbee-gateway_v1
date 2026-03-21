/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "matter_bridge.hpp"

namespace {

service::MatterBridgeSnapshot make_full_snapshot(uint32_t revision, uint16_t short_addr_base) {
    service::MatterBridgeSnapshot snapshot{};
    snapshot.revision = revision;
    snapshot.device_count = static_cast<uint16_t>(service::kServiceMaxDevices);

    for (std::size_t i = 0; i < service::kServiceMaxDevices; ++i) {
        auto& device = snapshot.devices[i];
        device.short_addr = static_cast<uint16_t>(short_addr_base + static_cast<uint16_t>(i));
        device.online = true;
        device.stale = false;
        device.primary_class = service::MatterBridgeDeviceClass::kTemperature;
        device.has_temperature = true;
        device.temperature_centi_c = static_cast<int16_t>(2000 + static_cast<int16_t>(i));
        device.has_occupancy = true;
        device.occupied = (i % 2U) == 0U;
        device.has_contact = true;
        device.contact_open = (i % 3U) == 0U;
    }

    return snapshot;
}

}  // namespace

int main() {
    matter_bridge::MatterBridge bridge;

    // start/stop should be idempotent and not crash.
    assert(bridge.start());
    assert(bridge.start());
    assert(bridge.started());

    const service::MatterBridgeSnapshot first = make_full_snapshot(1U, 0x1000U);
    const std::size_t first_count = bridge.sync_snapshot(first);
    assert(first_count == matter_bridge::kMatterMaxUpdatesPerSync);

    matter_bridge::MatterAttributeUpdate out[matter_bridge::kMatterMaxUpdatesPerSync]{};
    const std::size_t first_drained = bridge.drain_attribute_updates(out, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(first_drained == matter_bridge::kMatterMaxUpdatesPerSync);

    // A disjoint full snapshot would naturally exceed queue capacity; bridge must bound safely.
    const service::MatterBridgeSnapshot second = make_full_snapshot(2U, 0x4000U);
    const std::size_t second_count = bridge.sync_snapshot(second);
    assert(second_count == matter_bridge::kMatterMaxUpdatesPerSync);
    const std::size_t second_drained = bridge.drain_attribute_updates(out, matter_bridge::kMatterMaxUpdatesPerSync);
    assert(second_drained == matter_bridge::kMatterMaxUpdatesPerSync);

    bridge.stop();
    bridge.stop();
    assert(!bridge.started());
    assert(bridge.sync_snapshot(first) == 0U);
    assert(bridge.drain_attribute_updates(out, matter_bridge::kMatterMaxUpdatesPerSync) == 0U);

    return 0;
}
