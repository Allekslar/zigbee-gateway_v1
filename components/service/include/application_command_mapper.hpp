/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "application_requests.hpp"
#include "config_manager.hpp"

namespace service {

enum class ApplicationCommandParseStatus : uint8_t {
    kOk = 0,
    kInvalidPayload = 1,
    kInvalidTopic = 2,
    kInvalidShortAddr = 3,
    kInvalidProfileKey = 4,
    kInvalidProfileBounds = 5,
    kInvalidCapabilityFlags = 6,
};

const char* application_command_parse_error(ApplicationCommandParseStatus status) noexcept;

ApplicationCommandParseStatus parse_web_device_power_request(
    const char* body,
    DevicePowerCommandRequest* out_request) noexcept;

ApplicationCommandParseStatus parse_web_reporting_profile_request(
    const char* body,
    ReportingProfileWriteRequest* out_request) noexcept;

ApplicationCommandParseStatus parse_mqtt_device_power_request(
    const char* topic,
    const char* payload,
    DevicePowerCommandRequest* out_request) noexcept;

ApplicationCommandParseStatus parse_mqtt_reporting_profile_request(
    const char* topic,
    const char* payload,
    ReportingProfileWriteRequest* out_request) noexcept;

// Versioned MQTT command parsers (plan S4 MQTT #10-#13): topics are keyed
// by DeviceId hex (zigbee-gateway/v1/devices/<device_id>/power/set,
// .../config/set) instead of short_addr. out_device_id_hex_capacity must
// be at least 17 (16 hex characters + null terminator). The caller
// resolves device_id_hex -> short_addr (and, for the reporting profile
// case, -> DeviceId) via ServiceRuntimeApi, since this is a leaf
// topic/payload parser with no access to the locator registry -- mirrors
// the existing short_addr-keyed parsers' exact division of
// responsibility.
ApplicationCommandParseStatus parse_mqtt_v1_device_power_request(
    const char* topic,
    const char* payload,
    char* out_device_id_hex,
    std::size_t out_device_id_hex_capacity,
    bool* out_desired_power_on) noexcept;

ApplicationCommandParseStatus parse_mqtt_v1_reporting_profile_request(
    const char* topic,
    const char* payload,
    char* out_device_id_hex,
    std::size_t out_device_id_hex_capacity,
    ReportingProfileWriteRequest* out_request) noexcept;

bool mqtt_topic_has_suffix(const char* topic, const char* suffix) noexcept;

}  // namespace service
