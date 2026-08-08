/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdio>
#include <cstring>

#include "mqtt_discovery.hpp"

int main() {
    constexpr const char* kGatewayIdHex = "0012347788aa";
    constexpr const char* kDeviceIdHex = "00124b0001aa2201";

    service::MqttBridgeDeviceSnapshot device{};
    device.short_addr = 3105U;
    std::snprintf(device.device_id_hex.data(), device.device_id_hex.size(), "%s", kDeviceIdHex);
    device.online = true;
    device.power_on = true;
    device.has_temperature = true;
    device.occupancy_state = service::DeviceOccupancyState::kOccupied;
    device.contact_state = service::DeviceContactState::kClosed;
    device.has_battery = true;

    mqtt_bridge::HomeAssistantDiscoveryMessage messages[mqtt_bridge::kMaxDiscoveryMessagesPerDevice]{};
    const std::size_t count = mqtt_bridge::build_homeassistant_discovery_messages(
        kGatewayIdHex, device, messages, mqtt_bridge::kMaxDiscoveryMessagesPerDevice);
    assert(count == 5U);

    bool saw_switch = false;
    bool saw_temperature = false;
    bool saw_occupancy = false;
    bool saw_contact = false;
    bool saw_battery = false;

    for (std::size_t i = 0; i < count; ++i) {
        if (std::strstr(
                messages[i].topic,
                "homeassistant/switch/zigbee_gateway/zgw_0012347788aa_00124b0001aa2201_power/config") != nullptr) {
            saw_switch = true;
            assert(
                std::strstr(
                    messages[i].payload,
                    "\"command_topic\":\"zigbee-gateway/v1/devices/00124b0001aa2201/power/set\"") != nullptr);
            assert(
                std::strstr(
                    messages[i].payload,
                    "\"state_topic\":\"zigbee-gateway/v1/devices/00124b0001aa2201/state\"") != nullptr);
            assert(
                std::strstr(
                    messages[i].payload,
                    "\"identifiers\":[\"zgw_0012347788aa_00124b0001aa2201\"]") != nullptr);
        }
        if (std::strstr(
                messages[i].topic,
                "homeassistant/sensor/zigbee_gateway/zgw_0012347788aa_00124b0001aa2201_temperature/config") !=
            nullptr) {
            saw_temperature = true;
            assert(std::strstr(messages[i].payload, "\"device_class\":\"temperature\"") != nullptr);
            assert(
                std::strstr(
                    messages[i].payload,
                    "\"state_topic\":\"zigbee-gateway/v1/devices/00124b0001aa2201/telemetry\"") != nullptr);
        }
        if (std::strstr(
                messages[i].topic,
                "homeassistant/binary_sensor/zigbee_gateway/zgw_0012347788aa_00124b0001aa2201_occupancy/config") !=
            nullptr) {
            saw_occupancy = true;
            assert(std::strstr(messages[i].payload, "\"payload_on\":\"occupied\"") != nullptr);
        }
        if (std::strstr(
                messages[i].topic,
                "homeassistant/binary_sensor/zigbee_gateway/zgw_0012347788aa_00124b0001aa2201_contact/config") !=
            nullptr) {
            saw_contact = true;
            assert(std::strstr(messages[i].payload, "\"device_class\":\"door\"") != nullptr);
        }
        if (std::strstr(
                messages[i].topic,
                "homeassistant/sensor/zigbee_gateway/zgw_0012347788aa_00124b0001aa2201_battery/config") != nullptr) {
            saw_battery = true;
            assert(std::strstr(messages[i].payload, "\"device_class\":\"battery\"") != nullptr);
        }
        // Every entity's availability payload is JSON (v1), not the bare
        // "online"/"offline" string the legacy availability topic used --
        // HA needs a template to extract the boolean.
        assert(
            std::strstr(
                messages[i].payload,
                "\"availability_template\":\"{{ 'online' if value_json.online else 'offline' }}\"") != nullptr);
        assert(
            std::strstr(
                messages[i].payload,
                "\"availability_topic\":\"zigbee-gateway/v1/devices/00124b0001aa2201/availability\"") != nullptr);
    }

    assert(saw_switch);
    assert(saw_temperature);
    assert(saw_occupancy);
    assert(saw_contact);
    assert(saw_battery);

    // No messages without a resolved gateway_id, a resolved device
    // identity, an unknown short_addr, or an offline device.
    assert(
        mqtt_bridge::build_homeassistant_discovery_messages(
            nullptr, device, messages, mqtt_bridge::kMaxDiscoveryMessagesPerDevice) == 0U);
    service::MqttBridgeDeviceSnapshot no_identity = device;
    no_identity.device_id_hex[0] = '\0';
    assert(
        mqtt_bridge::build_homeassistant_discovery_messages(
            kGatewayIdHex, no_identity, messages, mqtt_bridge::kMaxDiscoveryMessagesPerDevice) == 0U);
    service::MqttBridgeDeviceSnapshot offline = device;
    offline.online = false;
    assert(
        mqtt_bridge::build_homeassistant_discovery_messages(
            kGatewayIdHex, offline, messages, mqtt_bridge::kMaxDiscoveryMessagesPerDevice) == 0U);

    service::MqttBridgeDeviceSnapshot previous = device;
    service::MqttBridgeDeviceSnapshot current = device;
    assert(!mqtt_bridge::discovery_schema_changed(previous, current));

    current.has_temperature = false;
    assert(mqtt_bridge::discovery_schema_changed(previous, current));

    // --- Legacy discovery tombstones (plan S4 MQTT #14/#17). ---
    mqtt_bridge::HomeAssistantDiscoveryMessage tombstones[mqtt_bridge::kMaxDiscoveryMessagesPerDevice]{};
    const std::size_t tombstone_count = mqtt_bridge::build_legacy_homeassistant_discovery_tombstones(
        3105U, tombstones, mqtt_bridge::kMaxDiscoveryMessagesPerDevice);
    assert(tombstone_count == 5U);

    bool saw_legacy_switch = false;
    bool saw_legacy_battery = false;
    for (std::size_t i = 0; i < tombstone_count; ++i) {
        assert(tombstones[i].payload[0] == '\0');
        assert(tombstones[i].retain);
        if (std::strcmp(tombstones[i].topic, "homeassistant/switch/zigbee_gateway/zgw_3105_power/config") == 0) {
            saw_legacy_switch = true;
        }
        if (std::strcmp(tombstones[i].topic, "homeassistant/sensor/zigbee_gateway/zgw_3105_battery/config") == 0) {
            saw_legacy_battery = true;
        }
    }
    assert(saw_legacy_switch);
    assert(saw_legacy_battery);

    // Undersized capacity and unknown short_addr are rejected/truncated cleanly.
    mqtt_bridge::HomeAssistantDiscoveryMessage small[2]{};
    assert(mqtt_bridge::build_legacy_homeassistant_discovery_tombstones(3105U, small, 2U) == 2U);
    assert(
        mqtt_bridge::build_legacy_homeassistant_discovery_tombstones(service::kUnknownShortAddr, tombstones, 5U) ==
        0U);

    return 0;
}
