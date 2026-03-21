/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "application_command_mapper.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace service {
namespace {

bool parse_u32_strict(const char* text, uint32_t* out) noexcept {
    if (text == nullptr || out == nullptr || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX) {
        return false;
    }

    *out = static_cast<uint32_t>(value);
    return true;
}

bool find_json_u32_field(const char* body, const char* key, uint32_t* out_value) noexcept {
    if (body == nullptr || key == nullptr || out_value == nullptr) {
        return false;
    }

    char pattern[64]{};
    const int pattern_written = std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pattern_written <= 0 || static_cast<std::size_t>(pattern_written) >= sizeof(pattern)) {
        return false;
    }

    const char* key_pos = std::strstr(body, pattern);
    if (key_pos == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key_pos + pattern_written, ':');
    if (colon == nullptr) {
        return false;
    }

    const char* value = colon + 1;
    while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') {
        ++value;
    }

    char number_buf[16]{};
    std::size_t idx = 0U;
    while (value[idx] >= '0' && value[idx] <= '9' && idx + 1U < sizeof(number_buf)) {
        number_buf[idx] = value[idx];
        ++idx;
    }
    number_buf[idx] = '\0';

    if (idx == 0U) {
        return false;
    }

    return parse_u32_strict(number_buf, out_value);
}

bool find_json_bool_field(const char* body, const char* key, bool* out_value) noexcept {
    if (body == nullptr || key == nullptr || out_value == nullptr) {
        return false;
    }

    char pattern[64]{};
    const int pattern_written = std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pattern_written <= 0 || static_cast<std::size_t>(pattern_written) >= sizeof(pattern)) {
        return false;
    }

    const char* key_pos = std::strstr(body, pattern);
    if (key_pos == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key_pos + pattern_written, ':');
    if (colon == nullptr) {
        return false;
    }

    const char* value = colon + 1;
    while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') {
        ++value;
    }

    if (std::strncmp(value, "true", 4) == 0) {
        *out_value = true;
        return true;
    }
    if (std::strncmp(value, "false", 5) == 0) {
        *out_value = false;
        return true;
    }

    return false;
}

ApplicationCommandParseStatus extract_device_short_addr_from_topic(
    const char* topic,
    const char* suffix,
    uint16_t* out_short_addr) noexcept {
    if (topic == nullptr || suffix == nullptr || out_short_addr == nullptr) {
        return ApplicationCommandParseStatus::kInvalidTopic;
    }

    constexpr const char* kPrefix = "zigbee-gateway/devices/";
    const std::size_t prefix_len = std::strlen(kPrefix);
    const std::size_t suffix_len = std::strlen(suffix);
    const std::size_t topic_len = std::strlen(topic);
    if (topic_len <= prefix_len + suffix_len) {
        return ApplicationCommandParseStatus::kInvalidTopic;
    }
    if (std::strncmp(topic, kPrefix, prefix_len) != 0) {
        return ApplicationCommandParseStatus::kInvalidTopic;
    }
    if (std::strcmp(topic + topic_len - suffix_len, suffix) != 0) {
        return ApplicationCommandParseStatus::kInvalidTopic;
    }

    const std::size_t short_len = topic_len - prefix_len - suffix_len;
    if (short_len == 0U || short_len >= 8U) {
        return ApplicationCommandParseStatus::kInvalidTopic;
    }

    char short_buf[8]{};
    std::memcpy(short_buf, topic + prefix_len, short_len);
    short_buf[short_len] = '\0';

    uint32_t short_raw = 0U;
    if (!parse_u32_strict(short_buf, &short_raw)) {
        return ApplicationCommandParseStatus::kInvalidTopic;
    }
    if (short_raw == 0U || short_raw > 0xFFFFU ||
        short_raw == static_cast<uint32_t>(kUnknownShortAddr)) {
        return ApplicationCommandParseStatus::kInvalidShortAddr;
    }

    *out_short_addr = static_cast<uint16_t>(short_raw);
    return ApplicationCommandParseStatus::kOk;
}

ApplicationCommandParseStatus parse_reporting_profile_payload(
    const char* payload,
    const uint16_t short_addr,
    ConfigManager::ReportingProfile* out_profile) noexcept {
    if (payload == nullptr || out_profile == nullptr) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    uint32_t endpoint = 0U;
    uint32_t cluster_id = 0U;
    uint32_t min_interval = 0U;
    uint32_t max_interval = 0U;
    if (!find_json_u32_field(payload, "endpoint", &endpoint) ||
        !find_json_u32_field(payload, "cluster_id", &cluster_id) ||
        !find_json_u32_field(payload, "min_interval_seconds", &min_interval) ||
        !find_json_u32_field(payload, "max_interval_seconds", &max_interval)) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    if (short_addr == 0U || short_addr == kUnknownShortAddr ||
        endpoint == 0U || endpoint > 0xFFU ||
        cluster_id == 0U || cluster_id > 0xFFFFU ||
        min_interval > 0xFFFFU || max_interval > 0xFFFFU) {
        return ApplicationCommandParseStatus::kInvalidProfileKey;
    }

    if (max_interval == 0U || min_interval > max_interval) {
        return ApplicationCommandParseStatus::kInvalidProfileBounds;
    }

    ConfigManager::ReportingProfile profile{};
    profile.in_use = true;
    profile.key.short_addr = short_addr;
    profile.key.endpoint = static_cast<uint8_t>(endpoint);
    profile.key.cluster_id = static_cast<uint16_t>(cluster_id);
    profile.min_interval_seconds = static_cast<uint16_t>(min_interval);
    profile.max_interval_seconds = static_cast<uint16_t>(max_interval);

    uint32_t reportable_change = 0U;
    (void)find_json_u32_field(payload, "reportable_change", &reportable_change);
    profile.reportable_change = reportable_change;

    uint32_t capability_flags = 0U;
    if (find_json_u32_field(payload, "capability_flags", &capability_flags)) {
        if (capability_flags > 0xFFU) {
            return ApplicationCommandParseStatus::kInvalidCapabilityFlags;
        }
        profile.capability_flags = static_cast<uint8_t>(capability_flags);
    }

    *out_profile = profile;
    return ApplicationCommandParseStatus::kOk;
}

}  // namespace

const char* application_command_parse_error(ApplicationCommandParseStatus status) noexcept {
    switch (status) {
        case ApplicationCommandParseStatus::kInvalidPayload:
            return "invalid_payload";
        case ApplicationCommandParseStatus::kInvalidTopic:
            return "invalid_topic";
        case ApplicationCommandParseStatus::kInvalidShortAddr:
            return "invalid_short_addr";
        case ApplicationCommandParseStatus::kInvalidProfileKey:
            return "invalid_profile_key";
        case ApplicationCommandParseStatus::kInvalidProfileBounds:
            return "invalid_profile_bounds";
        case ApplicationCommandParseStatus::kInvalidCapabilityFlags:
            return "invalid_capability_flags";
        case ApplicationCommandParseStatus::kOk:
        default:
            return "ok";
    }
}

bool mqtt_topic_has_suffix(const char* topic, const char* suffix) noexcept {
    if (topic == nullptr || suffix == nullptr) {
        return false;
    }

    const std::size_t topic_len = std::strlen(topic);
    const std::size_t suffix_len = std::strlen(suffix);
    if (topic_len < suffix_len) {
        return false;
    }

    return std::strcmp(topic + topic_len - suffix_len, suffix) == 0;
}

ApplicationCommandParseStatus parse_web_device_power_request(
    const char* body,
    DevicePowerCommandRequest* out_request) noexcept {
    if (body == nullptr || out_request == nullptr) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    uint32_t short_addr_raw = 0U;
    bool desired_power_on = false;
    if (!find_json_u32_field(body, "short_addr", &short_addr_raw) ||
        !find_json_bool_field(body, "power_on", &desired_power_on)) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }
    if (short_addr_raw == 0U || short_addr_raw > 0xFFFFU ||
        short_addr_raw == static_cast<uint32_t>(kUnknownShortAddr)) {
        return ApplicationCommandParseStatus::kInvalidShortAddr;
    }

    DevicePowerCommandRequest request{};
    request.short_addr = static_cast<uint16_t>(short_addr_raw);
    request.desired_power_on = desired_power_on;
    *out_request = request;
    return ApplicationCommandParseStatus::kOk;
}

ApplicationCommandParseStatus parse_web_reporting_profile_request(
    const char* body,
    ConfigManager::ReportingProfile* out_profile) noexcept {
    if (body == nullptr || out_profile == nullptr) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    uint32_t short_addr_raw = 0U;
    if (!find_json_u32_field(body, "short_addr", &short_addr_raw)) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }
    if (short_addr_raw == 0U || short_addr_raw > 0xFFFFU ||
        short_addr_raw == static_cast<uint32_t>(kUnknownShortAddr)) {
        return ApplicationCommandParseStatus::kInvalidProfileKey;
    }

    return parse_reporting_profile_payload(body, static_cast<uint16_t>(short_addr_raw), out_profile);
}

ApplicationCommandParseStatus parse_mqtt_device_power_request(
    const char* topic,
    const char* payload,
    DevicePowerCommandRequest* out_request) noexcept {
    if (payload == nullptr || out_request == nullptr) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    uint16_t short_addr = kUnknownShortAddr;
    const ApplicationCommandParseStatus short_addr_status =
        extract_device_short_addr_from_topic(topic, "/power/set", &short_addr);
    if (short_addr_status != ApplicationCommandParseStatus::kOk) {
        return short_addr_status;
    }

    bool desired_power_on = false;
    if (!find_json_bool_field(payload, "power_on", &desired_power_on)) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    DevicePowerCommandRequest request{};
    request.short_addr = short_addr;
    request.desired_power_on = desired_power_on;
    *out_request = request;
    return ApplicationCommandParseStatus::kOk;
}

ApplicationCommandParseStatus parse_mqtt_reporting_profile_request(
    const char* topic,
    const char* payload,
    ConfigManager::ReportingProfile* out_profile) noexcept {
    if (payload == nullptr || out_profile == nullptr) {
        return ApplicationCommandParseStatus::kInvalidPayload;
    }

    uint16_t short_addr = kUnknownShortAddr;
    const ApplicationCommandParseStatus short_addr_status =
        extract_device_short_addr_from_topic(topic, "/config", &short_addr);
    if (short_addr_status != ApplicationCommandParseStatus::kOk) {
        return short_addr_status;
    }

    return parse_reporting_profile_payload(payload, short_addr, out_profile);
}

}  // namespace service
