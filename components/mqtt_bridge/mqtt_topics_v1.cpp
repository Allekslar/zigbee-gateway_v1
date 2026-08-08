/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_topics_v1.hpp"

#include <cstdio>
#include <cstring>

namespace mqtt_bridge {
namespace {

constexpr const char* kV1TopicRoot = "zigbee-gateway/v1";
constexpr const char* kV1TopicDeviceConfigSetWildcard = "zigbee-gateway/v1/devices/+/config/set";
constexpr const char* kV1TopicDevicePowerSetWildcard = "zigbee-gateway/v1/devices/+/power/set";
constexpr const char* kV1TopicGatewayState = "zigbee-gateway/v1/gateway/state";

bool is_lowercase_hex(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool build_v1_device_topic(
    const char* device_id_hex, const char* suffix, char* out, const std::size_t out_size) noexcept {
    if (device_id_hex == nullptr || suffix == nullptr || out == nullptr || out_size == 0U) {
        return false;
    }
    if (std::strlen(device_id_hex) != kV1DeviceIdHexLength) {
        return false;
    }

    const int written =
        std::snprintf(out, out_size, "%s/devices/%s/%s", kV1TopicRoot, device_id_hex, suffix);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

}  // namespace

bool topic_v1_device_state(const char* device_id_hex, char* out, const std::size_t out_size) noexcept {
    return build_v1_device_topic(device_id_hex, "state", out, out_size);
}

bool topic_v1_device_telemetry(const char* device_id_hex, char* out, const std::size_t out_size) noexcept {
    return build_v1_device_topic(device_id_hex, "telemetry", out, out_size);
}

bool topic_v1_device_availability(const char* device_id_hex, char* out, const std::size_t out_size) noexcept {
    return build_v1_device_topic(device_id_hex, "availability", out, out_size);
}

bool topic_v1_device_config_set(const char* device_id_hex, char* out, const std::size_t out_size) noexcept {
    return build_v1_device_topic(device_id_hex, "config/set", out, out_size);
}

const char* topic_v1_device_config_set_wildcard() noexcept {
    return kV1TopicDeviceConfigSetWildcard;
}

bool topic_v1_device_power_set(const char* device_id_hex, char* out, const std::size_t out_size) noexcept {
    return build_v1_device_topic(device_id_hex, "power/set", out, out_size);
}

const char* topic_v1_device_power_set_wildcard() noexcept {
    return kV1TopicDevicePowerSetWildcard;
}

bool topic_v1_device_command_result(
    const char* device_id_hex, const uint32_t operation_id, char* out, const std::size_t out_size) noexcept {
    if (device_id_hex == nullptr || out == nullptr || out_size == 0U) {
        return false;
    }
    if (std::strlen(device_id_hex) != kV1DeviceIdHexLength) {
        return false;
    }

    const int written = std::snprintf(
        out, out_size, "%s/devices/%s/commands/%u/result", kV1TopicRoot, device_id_hex,
        static_cast<unsigned>(operation_id));
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

const char* topic_v1_gateway_state() noexcept {
    return kV1TopicGatewayState;
}

bool parse_v1_device_id_from_topic(
    const char* topic,
    const char* prefix,
    const char* suffix,
    char* out_hex,
    const std::size_t out_hex_capacity) noexcept {
    if (topic == nullptr || prefix == nullptr || suffix == nullptr || out_hex == nullptr ||
        out_hex_capacity < kV1DeviceIdHexLength + 1U) {
        return false;
    }

    const std::size_t topic_len = std::strlen(topic);
    const std::size_t prefix_len = std::strlen(prefix);
    const std::size_t suffix_len = std::strlen(suffix);
    if (topic_len != prefix_len + kV1DeviceIdHexLength + suffix_len) {
        return false;
    }
    if (std::strncmp(topic, prefix, prefix_len) != 0) {
        return false;
    }
    if (suffix_len > 0U && std::strcmp(topic + prefix_len + kV1DeviceIdHexLength, suffix) != 0) {
        return false;
    }

    const char* hex = topic + prefix_len;
    for (std::size_t i = 0; i < kV1DeviceIdHexLength; ++i) {
        if (!is_lowercase_hex(hex[i])) {
            return false;
        }
    }

    std::memcpy(out_hex, hex, kV1DeviceIdHexLength);
    out_hex[kV1DeviceIdHexLength] = '\0';
    return true;
}

}  // namespace mqtt_bridge
