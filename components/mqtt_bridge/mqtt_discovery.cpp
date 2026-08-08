/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_discovery.hpp"

#include <cstdio>
#include <cstring>

#include "mqtt_topics.hpp"
#include "mqtt_topics_v1.hpp"

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

// Versioned (plan S4 MQTT #16): unique_id/object_id/device identifiers are
// derived from both DeviceId and the canonical FD-17 GatewayId, so cloning
// the firmware image to different hardware yields a different identity
// while reboot/reset on the same hardware does not.
bool build_object_id(
    const char* gateway_id_hex,
    const char* device_id_hex,
    const char* suffix,
    char* out,
    const std::size_t out_size) noexcept {
    if (gateway_id_hex == nullptr || device_id_hex == nullptr || suffix == nullptr || out == nullptr ||
        out_size == 0U) {
        return false;
    }
    const int written = std::snprintf(out, out_size, "zgw_%s_%s_%s", gateway_id_hex, device_id_hex, suffix);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_device_name(const char* device_id_hex, char* out, const std::size_t out_size) noexcept {
    if (device_id_hex == nullptr || out == nullptr || out_size == 0U) {
        return false;
    }
    const int written = std::snprintf(out, out_size, "ZGW %s", device_id_hex);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_device_descriptor(
    const char* gateway_id_hex,
    const char* device_id_hex,
    char* out,
    const std::size_t out_size) noexcept {
    if (gateway_id_hex == nullptr || device_id_hex == nullptr) {
        return false;
    }

    char device_name[40]{};
    if (!build_device_name(device_id_hex, device_name, sizeof(device_name))) {
        return false;
    }

    const int written = std::snprintf(
        out,
        out_size,
        "\"device\":{\"identifiers\":[\"zgw_%s_%s\"],\"name\":\"%s\",\"manufacturer\":\"Alex.K.\",\"model\":\"Zigbee Gateway Device\"}",
        gateway_id_hex,
        device_id_hex,
        device_name);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

bool build_common_topics(
    const char* device_id_hex,
    char* availability_topic,
    const std::size_t availability_topic_size,
    char* state_topic,
    const std::size_t state_topic_size,
    char* telemetry_topic,
    const std::size_t telemetry_topic_size,
    char* power_set_topic,
    const std::size_t power_set_topic_size) noexcept {
    return topic_v1_device_availability(device_id_hex, availability_topic, availability_topic_size) &&
           topic_v1_device_state(device_id_hex, state_topic, state_topic_size) &&
           topic_v1_device_telemetry(device_id_hex, telemetry_topic, telemetry_topic_size) &&
           topic_v1_device_power_set(device_id_hex, power_set_topic, power_set_topic_size);
}

bool build_switch_discovery(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    const char* device_id_hex = device.device_id_hex.data();
    char object_id[64]{};
    char device_descriptor[192]{};
    char availability_topic[kV1TopicMaxLen]{};
    char state_topic[kV1TopicMaxLen]{};
    char telemetry_topic[kV1TopicMaxLen]{};
    char power_set_topic[kV1TopicMaxLen]{};
    char device_name[40]{};
    (void)telemetry_topic;

    if (!build_object_id(gateway_id_hex, device_id_hex, "power", object_id, sizeof(object_id)) ||
        !build_discovery_topic("switch", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(gateway_id_hex, device_id_hex, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device_id_hex, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device_id_hex,
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
        "\"availability_topic\":\"%s\",\"availability_template\":\"{{ 'online' if value_json.online else 'offline' }}\","
        "\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        power_set_topic,
        state_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_temperature_discovery(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || !device.has_temperature) {
        return false;
    }

    const char* device_id_hex = device.device_id_hex.data();
    char object_id[64]{};
    char device_descriptor[192]{};
    char availability_topic[kV1TopicMaxLen]{};
    char state_topic[kV1TopicMaxLen]{};
    char telemetry_topic[kV1TopicMaxLen]{};
    char power_set_topic[kV1TopicMaxLen]{};
    char device_name[40]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(gateway_id_hex, device_id_hex, "temperature", object_id, sizeof(object_id)) ||
        !build_discovery_topic("sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(gateway_id_hex, device_id_hex, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device_id_hex, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device_id_hex,
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
        "\"availability_topic\":\"%s\",\"availability_template\":\"{{ 'online' if value_json.online else 'offline' }}\","
        "\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_occupancy_discovery(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || device.occupancy_state == service::DeviceOccupancyState::kUnknown) {
        return false;
    }

    const char* device_id_hex = device.device_id_hex.data();
    char object_id[64]{};
    char device_descriptor[192]{};
    char availability_topic[kV1TopicMaxLen]{};
    char state_topic[kV1TopicMaxLen]{};
    char telemetry_topic[kV1TopicMaxLen]{};
    char power_set_topic[kV1TopicMaxLen]{};
    char device_name[40]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(gateway_id_hex, device_id_hex, "occupancy", object_id, sizeof(object_id)) ||
        !build_discovery_topic("binary_sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(gateway_id_hex, device_id_hex, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device_id_hex, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device_id_hex,
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
        "\"device_class\":\"occupancy\",\"availability_topic\":\"%s\","
        "\"availability_template\":\"{{ 'online' if value_json.online else 'offline' }}\","
        "\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_contact_discovery(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || device.contact_state == service::DeviceContactState::kUnknown) {
        return false;
    }

    const char* device_id_hex = device.device_id_hex.data();
    char object_id[64]{};
    char device_descriptor[192]{};
    char availability_topic[kV1TopicMaxLen]{};
    char state_topic[kV1TopicMaxLen]{};
    char telemetry_topic[kV1TopicMaxLen]{};
    char power_set_topic[kV1TopicMaxLen]{};
    char device_name[40]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(gateway_id_hex, device_id_hex, "contact", object_id, sizeof(object_id)) ||
        !build_discovery_topic("binary_sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(gateway_id_hex, device_id_hex, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device_id_hex, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device_id_hex,
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
        "\"device_class\":\"door\",\"availability_topic\":\"%s\","
        "\"availability_template\":\"{{ 'online' if value_json.online else 'offline' }}\","
        "\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_battery_discovery(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || !device.has_battery) {
        return false;
    }

    const char* device_id_hex = device.device_id_hex.data();
    char object_id[64]{};
    char device_descriptor[192]{};
    char availability_topic[kV1TopicMaxLen]{};
    char state_topic[kV1TopicMaxLen]{};
    char telemetry_topic[kV1TopicMaxLen]{};
    char power_set_topic[kV1TopicMaxLen]{};
    char device_name[40]{};
    (void)state_topic;
    (void)power_set_topic;

    if (!build_object_id(gateway_id_hex, device_id_hex, "battery", object_id, sizeof(object_id)) ||
        !build_discovery_topic("sensor", object_id, out->topic, sizeof(out->topic)) ||
        !build_device_descriptor(gateway_id_hex, device_id_hex, device_descriptor, sizeof(device_descriptor)) ||
        !build_device_name(device_id_hex, device_name, sizeof(device_name)) ||
        !build_common_topics(
            device_id_hex,
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
        "\"availability_topic\":\"%s\",\"availability_template\":\"{{ 'online' if value_json.online else 'offline' }}\","
        "\"payload_available\":\"online\",\"payload_not_available\":\"offline\",%s}",
        device_name,
        object_id,
        telemetry_topic,
        availability_topic,
        device_descriptor);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out->payload);
}

bool build_legacy_discovery_tombstone(
    const char* component, const uint16_t short_addr, const char* suffix, HomeAssistantDiscoveryMessage* out) noexcept {
    if (out == nullptr || component == nullptr || suffix == nullptr) {
        return false;
    }

    char object_id[48]{};
    const int object_id_written =
        std::snprintf(object_id, sizeof(object_id), "zgw_%u_%s", static_cast<unsigned>(short_addr), suffix);
    if (object_id_written <= 0 || static_cast<std::size_t>(object_id_written) >= sizeof(object_id)) {
        return false;
    }
    if (!build_discovery_topic(component, object_id, out->topic, sizeof(out->topic))) {
        return false;
    }

    out->payload[0] = '\0';
    out->retain = true;
    return true;
}

}  // namespace

std::size_t build_homeassistant_discovery_messages(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out,
    const std::size_t capacity) noexcept {
    if (out == nullptr || capacity == 0U || gateway_id_hex == nullptr ||
        device.short_addr == service::kUnknownShortAddr || !device.online || device.device_id_hex[0] == '\0') {
        return 0U;
    }

    std::size_t count = 0U;
    HomeAssistantDiscoveryMessage message{};

    if (count < capacity && build_switch_discovery(gateway_id_hex, device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_temperature_discovery(gateway_id_hex, device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_occupancy_discovery(gateway_id_hex, device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_contact_discovery(gateway_id_hex, device, &message)) {
        out[count++] = message;
    }
    if (count < capacity && build_battery_discovery(gateway_id_hex, device, &message)) {
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

std::size_t build_legacy_homeassistant_discovery_tombstones(
    const uint16_t short_addr, HomeAssistantDiscoveryMessage* out, const std::size_t capacity) noexcept {
    if (out == nullptr || capacity == 0U || short_addr == service::kUnknownShortAddr) {
        return 0U;
    }

    struct LegacyEntity {
        const char* component;
        const char* suffix;
    };
    constexpr LegacyEntity kLegacyEntities[] = {
        {"switch", "power"},
        {"sensor", "temperature"},
        {"binary_sensor", "occupancy"},
        {"binary_sensor", "contact"},
        {"sensor", "battery"},
    };

    std::size_t count = 0U;
    for (const LegacyEntity& entity : kLegacyEntities) {
        if (count >= capacity) {
            break;
        }
        HomeAssistantDiscoveryMessage message{};
        if (build_legacy_discovery_tombstone(entity.component, short_addr, entity.suffix, &message)) {
            out[count++] = message;
        }
    }
    return count;
}

}  // namespace mqtt_bridge
