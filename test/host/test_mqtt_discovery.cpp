/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "mqtt_discovery.hpp"

int main() {
    service::MqttBridgeDeviceSnapshot device{};
    device.short_addr = 3105U;
    device.online = true;
    device.power_on = true;
    device.has_temperature = true;
    device.occupancy_state = service::DeviceOccupancyState::kOccupied;
    device.contact_state = service::DeviceContactState::kClosed;
    device.has_battery = true;

    mqtt_bridge::HomeAssistantDiscoveryMessage messages[mqtt_bridge::kMaxDiscoveryMessagesPerDevice]{};
    const std::size_t count =
        mqtt_bridge::build_homeassistant_discovery_messages(device, messages, mqtt_bridge::kMaxDiscoveryMessagesPerDevice);
    assert(count == 5U);

    bool saw_switch = false;
    bool saw_temperature = false;
    bool saw_occupancy = false;
    bool saw_contact = false;
    bool saw_battery = false;

    for (std::size_t i = 0; i < count; ++i) {
        if (std::strstr(messages[i].topic, "homeassistant/switch/zigbee_gateway/zgw_3105_power/config") != nullptr) {
            saw_switch = true;
            assert(std::strstr(messages[i].payload, "\"command_topic\":\"zigbee-gateway/devices/3105/power/set\"") !=
                   nullptr);
        }
        if (std::strstr(messages[i].topic, "homeassistant/sensor/zigbee_gateway/zgw_3105_temperature/config") !=
            nullptr) {
            saw_temperature = true;
            assert(std::strstr(messages[i].payload, "\"device_class\":\"temperature\"") != nullptr);
        }
        if (std::strstr(messages[i].topic, "homeassistant/binary_sensor/zigbee_gateway/zgw_3105_occupancy/config") !=
            nullptr) {
            saw_occupancy = true;
            assert(std::strstr(messages[i].payload, "\"payload_on\":\"occupied\"") != nullptr);
        }
        if (std::strstr(messages[i].topic, "homeassistant/binary_sensor/zigbee_gateway/zgw_3105_contact/config") !=
            nullptr) {
            saw_contact = true;
            assert(std::strstr(messages[i].payload, "\"device_class\":\"door\"") != nullptr);
        }
        if (std::strstr(messages[i].topic, "homeassistant/sensor/zigbee_gateway/zgw_3105_battery/config") != nullptr) {
            saw_battery = true;
            assert(std::strstr(messages[i].payload, "\"device_class\":\"battery\"") != nullptr);
        }
    }

    assert(saw_switch);
    assert(saw_temperature);
    assert(saw_occupancy);
    assert(saw_contact);
    assert(saw_battery);

    service::MqttBridgeDeviceSnapshot previous = device;
    service::MqttBridgeDeviceSnapshot current = device;
    assert(!mqtt_bridge::discovery_schema_changed(previous, current));

    current.has_temperature = false;
    assert(mqtt_bridge::discovery_schema_changed(previous, current));

    return 0;
}
