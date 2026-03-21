/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_discovery.hpp"

#include <cstdio>
#include <cstring>

#include "mqtt_topics.hpp"

namespace mqtt_bridge {
namespace {

constexpr const char* kDiscoveryPrefix = "homeassistant";
constexpr const char* kNodeId = "zigbee_gateway";

bool build_discovery_topic(
    const char* component,
    const char* object_id,
    char* out,
    const std::size_t out_size) noexcept {
    if (component == nullptr || object_id == nullptr || out == nullptr || out_size == 0U) {
        return false;
    }

    const int written = std::snprintf(
        out,
        out_size,
        "%s/%s/%s/%s/config",
        kDiscoveryPrefix,
        component,
        kNodeId,
        object_id);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_device_name(const uint16_t short_addr, char* out, const std::size_t out_size) noexcept {
    if (out == nullptr || out_size == 0U) {
        return false;
    }
    const int written = std::snprintf(out, out_size, "ZGW %u", static_cast<unsigned>(short_addr));
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_object_id(
    const uint16_t short_addr,
    const char* suffix,
    char* out,
    const std::size_t out_size) noexcept {
    if (suffix == nullptr || out == nullptr || out_size == 0U) {
        return false;
    }
    const int written = std::snprintf(out, out_size, "zgw_%u_%s", static_cast<unsigned>(short_addr), suffix);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_device_descriptor(
    const uint16_t short_addr,
    char* out,
    const std::size_t out_size) noexcept {
    char device_name[32]{};
    if (!build_device_name(short_addr, device_name, sizeof(device_name))) {
        return false;
    }

    const int written = std::snprintf(
        out,
        out_size,
        "\"device\":{\"identifiers\":[\"zgw_%u\"],\"name\":\"%s\",\"manufacturer\":\"Alex.K.\",\"model\":\"Zigbee Gateway Device\"}",
        static_cast<unsigned>(short_addr),
        device_name);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_common_topics(
    const uint16_t short_addr,
    char* availability_topic,
    const std::size_t availability_topic_size,
    char* state_topic,
    const std::size_t state_topic_size,
    char* telemetry_topic,
    const std::size_t telemetry_topic_size,
    char* power_set_topic,
    const std::size_t power_set_topic_size) noexcept {
    return topic_device_availability(short_addr, availability_topic, availability_topic_size) &&
           topic_device_state(short_addr, state_topic, state_topic_size) &&
           topic_device_telemetry(short_addr, telemetry_topic, telemetry_topic_size) &&
           topic_device_power_set(short_addr, power_set_topic, power_set_topic_size);
}

bool build_switch_discovery(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    char object_id[48]{};
    char device_descriptor[192]{};
    char availability_topic[kTopicMaxLen]{};
    char state_topic[kTopicMaxLen]{};
    char telemetry_topic[kTopicMaxLen]{};
    char power_set_topic[kTopicMaxLen]{};
    char device_name[32]{};
    (void)telemetry_topic;

    if (!build_object_id(device.short_addr, "power", object_id, sizeof(object_id)) ||
        !build_discovery_topic("switch", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(device.short_addr, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device.short_addr, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device.short_addr,
            availability_topic,
            sizeof(availability_topic),
            state_topic,
            sizeof(state_topic),
            telemetry_topic,
            sizeof(telemetry_topic),
            power_set_topic,
            sizeof(power_set_topic))) {
        return false;
    }

    const int written = std::snprintf(
        out->payload,
        sizeof(out->payload),
        "{\"name\":\"%s Power\",\"unique_id\":\"%s\",\"command_topic\":\"%s\",\"payload_on\":\"{\\\"power_on\\\":true}\","
        "\"payload_off\":\"{\\\"power_on\\\":false}\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ 'ON' if value_json.power_on else 'OFF' }}\",\"state_on\":\"ON\",\"state_off\":\"OFF\","
        "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        power_set_topic,
        state_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_temperature_discovery(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || !device.has_temperature) {
        return false;
    }

    char object_id[48]{};
    char device_descriptor[192]{};
    char availability_topic[kTopicMaxLen]{};
    char state_topic[kTopicMaxLen]{};
    char telemetry_topic[kTopicMaxLen]{};
    char power_set_topic[kTopicMaxLen]{};
    char device_name[32]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(device.short_addr, "temperature", object_id, sizeof(object_id)) ||
        !build_discovery_topic("sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(device.short_addr, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device.short_addr, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device.short_addr,
            availability_topic,
            sizeof(availability_topic),
            state_topic,
            sizeof(state_topic),
            telemetry_topic,
            sizeof(telemetry_topic),
            power_set_topic,
            sizeof(power_set_topic))) {
        return false;
    }

    const int written = std::snprintf(
        out->payload,
        sizeof(out->payload),
        "{\"name\":\"%s Temperature\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.temperature_c }}\",\"unit_of_measurement\":\"°C\","
        "\"device_class\":\"temperature\",\"state_class\":\"measurement\","
        "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_occupancy_discovery(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || device.occupancy_state == service::DeviceOccupancyState::kUnknown) {
        return false;
    }

    char object_id[48]{};
    char device_descriptor[192]{};
    char availability_topic[kTopicMaxLen]{};
    char state_topic[kTopicMaxLen]{};
    char telemetry_topic[kTopicMaxLen]{};
    char power_set_topic[kTopicMaxLen]{};
    char device_name[32]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(device.short_addr, "occupancy", object_id, sizeof(object_id)) ||
        !build_discovery_topic("binary_sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(device.short_addr, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device.short_addr, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device.short_addr,
            availability_topic,
            sizeof(availability_topic),
            state_topic,
            sizeof(state_topic),
            telemetry_topic,
            sizeof(telemetry_topic),
            power_set_topic,
            sizeof(power_set_topic))) {
        return false;
    }

    const int written = std::snprintf(
        out->payload,
        sizeof(out->payload),
        "{\"name\":\"%s Occupancy\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.occupancy }}\",\"payload_on\":\"occupied\",\"payload_off\":\"not_occupied\","
        "\"device_class\":\"occupancy\",\"availability_topic\":\"%s\",\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_contact_discovery(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || device.contact_state == service::DeviceContactState::kUnknown) {
        return false;
    }

    char object_id[48]{};
    char device_descriptor[192]{};
    char availability_topic[kTopicMaxLen]{};
    char state_topic[kTopicMaxLen]{};
    char telemetry_topic[kTopicMaxLen]{};
    char power_set_topic[kTopicMaxLen]{};
    char device_name[32]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(device.short_addr, "contact", object_id, sizeof(object_id)) ||
        !build_discovery_topic("binary_sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(device.short_addr, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device.short_addr, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device.short_addr,
            availability_topic,
            sizeof(availability_topic),
            state_topic,
            sizeof(state_topic),
            telemetry_topic,
            sizeof(telemetry_topic),
            power_set_topic,
            sizeof(power_set_topic))) {
        return false;
    }

    const int written = std::snprintf(
        out->payload,
        sizeof(out->payload),
        "{\"name\":\"%s Contact\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.contact.state }}\",\"payload_on\":\"open\",\"payload_off\":\"closed\","
        "\"device_class\":\"door\",\"availability_topic\":\"%s\",\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_battery_discovery(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || !device.has_battery) {
        return false;
    }

    char object_id[48]{};
    char device_descriptor[192]{};
    char availability_topic[kTopicMaxLen]{};
    char state_topic[kTopicMaxLen]{};
    char telemetry_topic[kTopicMaxLen]{};
    char power_set_topic[kTopicMaxLen]{};
    char device_name[32]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(device.short_addr, "battery", object_id, sizeof(object_id)) ||
        !build_discovery_topic("sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(device.short_addr, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device.short_addr, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device.short_addr,
            availability_topic,
            sizeof(availability_topic),
            state_topic,
            sizeof(state_topic),
            telemetry_topic,
            sizeof(telemetry_topic),
            power_set_topic,
            sizeof(power_set_topic))) {
        return false;
    }

    const int written = std::snprintf(
        out->payload,
        sizeof(out->payload),
        "{\"name\":\"%s Battery\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.battery.percent }}\",\"unit_of_measurement\":\"%%\","
        "\"device_class\":\"battery\",\"state_class\":\"measurement\","
        "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

}  // namespace

std::size_t build_homeassistant_discovery_messages(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out,
    const std::size_t capacity) noexcept {
    if (out == nullptr || capacity == 0U || device.short_addr == service::kUnknownShortAddr || !device.online) {
        return 0U;
    }

    std::size_t count = 0U;
    HomeAssistantDiscoveryMessage message{};

    if (count < capacity && build_switch_discovery(device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_temperature_discovery(device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_occupancy_discovery(device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_contact_discovery(device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_battery_discovery(device, &message)) {
        out[count++] = message;
    }

    return count;
}

bool discovery_schema_changed(
    const service::MqttBridgeDeviceSnapshot& previous,
    const service::MqttBridgeDeviceSnapshot& current) noexcept {
    return previous.has_temperature != current.has_temperature ||
           (previous.occupancy_state == service::DeviceOccupancyState::kUnknown) !=
               (current.occupancy_state == service::DeviceOccupancyState::kUnknown) ||
           (previous.contact_state == service::DeviceContactState::kUnknown) !=
               (current.contact_state == service::DeviceContactState::kUnknown) ||
           previous.has_battery != current.has_battery;
}

}  // namespace mqtt_bridge
