/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "application_command_mapper.hpp"

int main() {
    service::DevicePowerCommandRequest power_request{};
    assert(
        service::parse_web_device_power_request(
            "{\"short_addr\":8705,\"power_on\":true}",
            &power_request) == service::ApplicationCommandParseStatus::kOk);
    assert(power_request.short_addr == 0x2201U);
    assert(power_request.desired_power_on);

    assert(
        service::parse_web_device_power_request(
            "{\"short_addr\":65535,\"power_on\":true}",
            &power_request) == service::ApplicationCommandParseStatus::kInvalidShortAddr);

    service::ConfigManager::ReportingProfile profile{};
    assert(
        service::parse_web_reporting_profile_request(
            "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,"
            "\"min_interval_seconds\":10,\"max_interval_seconds\":300,"
            "\"reportable_change\":25,\"capability_flags\":3}",
            &profile) == service::ApplicationCommandParseStatus::kOk);
    assert(profile.key.short_addr == 0x2201U);
    assert(profile.key.endpoint == 1U);
    assert(profile.key.cluster_id == 0x0402U);
    assert(profile.min_interval_seconds == 10U);
    assert(profile.max_interval_seconds == 300U);
    assert(profile.reportable_change == 25U);
    assert(profile.capability_flags == 3U);

    assert(
        service::parse_web_reporting_profile_request(
            "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,"
            "\"min_interval_seconds\":301,\"max_interval_seconds\":300}",
            &profile) == service::ApplicationCommandParseStatus::kInvalidProfileBounds);

    assert(
        service::parse_mqtt_device_power_request(
            "zigbee-gateway/devices/8705/power/set",
            "{\"power_on\":false}",
            &power_request) == service::ApplicationCommandParseStatus::kOk);
    assert(power_request.short_addr == 0x2201U);
    assert(!power_request.desired_power_on);

    assert(
        service::parse_mqtt_reporting_profile_request(
            "zigbee-gateway/devices/8705/config",
            "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,"
            "\"max_interval_seconds\":300,\"capability_flags\":2}",
            &profile) == service::ApplicationCommandParseStatus::kOk);
    assert(profile.key.short_addr == 0x2201U);
    assert(profile.capability_flags == 2U);

    assert(
        service::parse_mqtt_reporting_profile_request(
            "zigbee-gateway/devices/8705/config",
            "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,"
            "\"max_interval_seconds\":300,\"capability_flags\":512}",
            &profile) == service::ApplicationCommandParseStatus::kInvalidCapabilityFlags);

    assert(
        service::parse_mqtt_device_power_request(
            "zigbee-gateway/devices/nope/power/set",
            "{\"power_on\":true}",
            &power_request) == service::ApplicationCommandParseStatus::kInvalidTopic);

    return 0;
}
