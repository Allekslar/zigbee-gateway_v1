/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "mqtt_bridge.hpp"

namespace {

core::CoreState make_single_device_state(
    const uint16_t short_addr,
    const bool online,
    const bool power_on,
    const uint32_t last_report_at_ms,
    const bool stale,
    const uint8_t lqi,
    const int8_t rssi_dbm) {
    core::CoreState state{};
    state.device_count = 1;
    state.devices[0].short_addr = short_addr;
    state.devices[0].online = online;
    state.devices[0].power_on = power_on;
    state.devices[0].has_temperature = true;
    state.devices[0].temperature_centi_c = 2150;
    state.devices[0].occupancy_state = core::CoreOccupancyState::kOccupied;
    state.devices[0].contact_state = core::CoreContactState::kClosed;
    state.devices[0].contact_tamper = false;
    state.devices[0].contact_battery_low = false;
    state.devices[0].has_battery = true;
    state.devices[0].battery_percent = 74;
    state.devices[0].has_battery_voltage = true;
    state.devices[0].battery_voltage_mv = 3000;
    state.devices[0].has_lqi = true;
    state.devices[0].lqi = lqi;
    state.devices[0].has_rssi = true;
    state.devices[0].rssi_dbm = rssi_dbm;
    state.devices[0].stale = stale;
    state.devices[0].last_report_at_ms = last_report_at_ms;
    return state;
}

bool has_topic(const mqtt_bridge::MqttPublishedMessage* messages, std::size_t count, const char* topic) {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(messages[i].topic, topic) == 0) {
            return true;
        }
    }
    return false;
}

const mqtt_bridge::MqttPublishedMessage* find_topic(
    const mqtt_bridge::MqttPublishedMessage* messages,
    std::size_t count,
    const char* topic) {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(messages[i].topic, topic) == 0) {
            return &messages[i];
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    mqtt_bridge::MqttBridge bridge;

    core::CoreState first = make_single_device_state(0x2201, true, true, 4242, false, 200, -63);

    assert(bridge.sync_snapshot(first) == 0U);

    assert(bridge.start());

    const std::size_t first_count = bridge.sync_snapshot(first);
    assert(first_count == 3U);

    mqtt_bridge::MqttPublishedMessage out[mqtt_bridge::kMaxMqttPublicationsPerSync]{};
    std::size_t drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 3U);

    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/availability"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/state"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/telemetry"));

    const mqtt_bridge::MqttPublishedMessage* availability =
        find_topic(out, drained, "zigbee-gateway/devices/8705/availability");
    assert(availability != nullptr);
    assert(std::strcmp(availability->payload, "online") == 0);

    assert(bridge.sync_snapshot(first) == 0U);
    assert(bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync) == 0U);

    core::CoreState telemetry_changed = first;
    telemetry_changed.devices[0].lqi = 180;
    telemetry_changed.devices[0].rssi_dbm = -70;
    telemetry_changed.devices[0].last_report_at_ms = 5000;
    telemetry_changed.devices[0].stale = true;
    assert(bridge.sync_snapshot(telemetry_changed) == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/devices/8705/telemetry") == 0);
    assert(std::strstr(out[0].payload, "\"stale\":true") != nullptr);

    core::CoreState power_changed = telemetry_changed;
    power_changed.devices[0].power_on = false;
    assert(bridge.sync_snapshot(power_changed) == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/devices/8705/state") == 0);
    assert(std::strcmp(out[0].payload, "{\"power_on\":false}") == 0);

    core::CoreState removed{};
    removed.device_count = 0;
    assert(bridge.sync_snapshot(removed) == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/devices/8705/availability") == 0);
    assert(std::strcmp(out[0].payload, "offline") == 0);

    bridge.stop();
    assert(bridge.sync_snapshot(first) == 0U);

    return 0;
}
