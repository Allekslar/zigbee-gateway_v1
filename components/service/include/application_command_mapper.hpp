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
    ConfigManager::ReportingProfile* out_profile) noexcept;

ApplicationCommandParseStatus parse_mqtt_device_power_request(
    const char* topic,
    const char* payload,
    DevicePowerCommandRequest* out_request) noexcept;

ApplicationCommandParseStatus parse_mqtt_reporting_profile_request(
    const char* topic,
    const char* payload,
    ConfigManager::ReportingProfile* out_profile) noexcept;

bool mqtt_topic_has_suffix(const char* topic, const char* suffix) noexcept;

}  // namespace service
