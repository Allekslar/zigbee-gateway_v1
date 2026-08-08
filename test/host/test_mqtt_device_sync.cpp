/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "mqtt_bridge.hpp"
#include "mqtt_bridge_test_access.hpp"

namespace {

constexpr const char* kDeviceIdHexA = "00124b0001aa2201";
constexpr const char* kDeviceIdHexB = "00124b0001aa2202";
constexpr const char* kGatewayIdHex = "0012347788aa";

service::MqttBridgeSnapshot make_single_device_snapshot(
    const uint16_t short_addr,
    const char* device_id_hex,
    const bool online,
    const bool power_on,
    const uint32_t last_report_at_ms,
    const bool stale,
    const uint8_t lqi,
    const int8_t rssi_dbm) {
    service::MqttBridgeSnapshot snapshot{};
    snapshot.device_count = 1;
    snapshot.devices[0].short_addr = short_addr;
    if (device_id_hex != nullptr) {
        std::snprintf(
            snapshot.devices[0].device_id_hex.data(), snapshot.devices[0].device_id_hex.size(), "%s", device_id_hex);
    }
    snapshot.devices[0].online = online;
    snapshot.devices[0].power_on = power_on;
    snapshot.devices[0].has_temperature = true;
    snapshot.devices[0].temperature_centi_c = 2150;
    snapshot.devices[0].occupancy_state = service::DeviceOccupancyState::kOccupied;
    snapshot.devices[0].contact_state = service::DeviceContactState::kClosed;
    snapshot.devices[0].contact_tamper = false;
    snapshot.devices[0].contact_battery_low = false;
    snapshot.devices[0].has_battery = true;
    snapshot.devices[0].battery_percent = 74;
    snapshot.devices[0].has_battery_voltage = true;
    snapshot.devices[0].battery_voltage_mv = 3000;
    snapshot.devices[0].has_lqi = true;
    snapshot.devices[0].lqi = lqi;
    snapshot.devices[0].has_rssi = true;
    snapshot.devices[0].rssi_dbm = rssi_dbm;
    snapshot.devices[0].stale = stale;
    snapshot.devices[0].last_report_at_ms = last_report_at_ms;
    return snapshot;
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

    service::MqttBridgeSnapshot first =
        make_single_device_snapshot(0x2201, kDeviceIdHexA, true, true, 4242, false, 200, -63);

    assert(bridge.sync_snapshot(first) == 0U);

    assert(bridge.start());
    // sync_snapshot() alone (unlike sync_runtime_snapshot()) never touches
    // ServiceRuntime, so there is no runtime to resolve gateway_id from;
    // the test seam sets it directly to exercise v1 publishing.
    mqtt_bridge::MqttBridgeTestAccess::set_gateway_id_hex_for_test(bridge, kGatewayIdHex);

    // First sync after start(): 3 v1 publications (availability/state/
    // telemetry) plus, once per boot, 3 legacy retained-empty-payload
    // tombstones for the same device's legacy short_addr topics (plan S4
    // MQTT #14).
    const std::size_t first_count = bridge.sync_snapshot(first);
    assert(first_count == 6U);

    mqtt_bridge::MqttPublishedMessage out[mqtt_bridge::kMaxMqttPublicationsPerSync]{};
    std::size_t drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 6U);

    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/availability"));
    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/state"));
    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/telemetry"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/availability"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/state"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/telemetry"));

    const mqtt_bridge::MqttPublishedMessage* v1_availability =
        find_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/availability");
    assert(v1_availability != nullptr);
    assert(std::strstr(v1_availability->payload, "\"schema_version\":1") != nullptr);
    assert(std::strstr(v1_availability->payload, "\"gateway_id\":\"0012347788aa\"") != nullptr);
    assert(std::strstr(v1_availability->payload, "\"device_id\":\"00124b0001aa2201\"") != nullptr);
    assert(std::strstr(v1_availability->payload, "\"online\":true") != nullptr);

    // Legacy topics carry only the one-time empty tombstone, never real
    // content, after cutover.
    const mqtt_bridge::MqttPublishedMessage* legacy_state =
        find_topic(out, drained, "zigbee-gateway/devices/8705/state");
    assert(legacy_state != nullptr);
    assert(legacy_state->payload[0] == '\0');
    assert(legacy_state->retain);

    // Second sync, unchanged snapshot: no publications at all, and the
    // one-time tombstone sweep never fires again.
    assert(bridge.sync_snapshot(first) == 0U);
    assert(bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync) == 0U);

    service::MqttBridgeSnapshot telemetry_changed = first;
    telemetry_changed.devices[0].lqi = 180;
    telemetry_changed.devices[0].rssi_dbm = -70;
    telemetry_changed.devices[0].last_report_at_ms = 5000;
    telemetry_changed.devices[0].stale = true;
    assert(bridge.sync_snapshot(telemetry_changed) == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/v1/devices/00124b0001aa2201/telemetry") == 0);
    assert(std::strstr(out[0].payload, "\"stale\":true") != nullptr);

    service::MqttBridgeSnapshot power_changed = telemetry_changed;
    power_changed.devices[0].power_on = false;
    assert(bridge.sync_snapshot(power_changed) == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/v1/devices/00124b0001aa2201/state") == 0);
    assert(std::strstr(out[0].payload, "\"power_on\":false") != nullptr);

    // Remap: same DeviceId, a different short_addr. Identity-keyed
    // diffing (plan S4 MQTT #15) recognizes this as the same device, not
    // a disappearance+recreation -- no availability flicker, and no
    // legacy topic is touched for the *new* short_addr.
    service::MqttBridgeSnapshot remapped = power_changed;
    remapped.devices[0].short_addr = 0x2299;
    assert(bridge.sync_snapshot(remapped) == 0U);
    assert(bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync) == 0U);

    service::MqttBridgeSnapshot removed{};
    removed.device_count = 0;
    assert(bridge.sync_snapshot(removed) == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/v1/devices/00124b0001aa2201/availability") == 0);
    assert(std::strstr(out[0].payload, "\"online\":false") != nullptr);

    bridge.stop();
    assert(bridge.sync_snapshot(first) == 0U);

    // --- A second device, resolved after the first sync (identity
    // resolves late): the tombstone sweep already ran for the boot, but
    // this device is new to that sweep's original snapshot, so it still
    // gets tombstoned the first time it appears active, since
    // cache_initialized_ was reset by stop()/would be reset again by a
    // fresh start(). ---
    mqtt_bridge::MqttBridge second_bridge;
    assert(second_bridge.start());
    mqtt_bridge::MqttBridgeTestAccess::set_gateway_id_hex_for_test(second_bridge, kGatewayIdHex);

    service::MqttBridgeSnapshot no_identity_yet =
        make_single_device_snapshot(0x2202, nullptr, true, false, 100, false, 10, -50);
    // No v1 publications (no resolved DeviceId yet), but the legacy
    // tombstone sweep still fires for this short_addr on the first sync.
    assert(second_bridge.sync_snapshot(no_identity_yet) == 3U);
    drained = second_bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 3U);
    assert(has_topic(out, drained, "zigbee-gateway/devices/8706/availability"));
    assert(!has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2202/availability"));

    service::MqttBridgeSnapshot identity_resolved =
        make_single_device_snapshot(0x2202, kDeviceIdHexB, true, false, 100, false, 10, -50);
    // Second sync: identity now resolved -- v1 publications appear, but no
    // further legacy tombstone (the one-time sweep already happened).
    assert(second_bridge.sync_snapshot(identity_resolved) == 3U);
    drained = second_bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 3U);
    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2202/availability"));
    assert(!has_topic(out, drained, "zigbee-gateway/devices/8706/availability"));

    return 0;
}
