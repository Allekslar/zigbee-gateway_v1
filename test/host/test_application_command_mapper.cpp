/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

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

    service::ReportingProfileWriteRequest request{};
    assert(
        service::parse_web_reporting_profile_request(
            "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,"
            "\"min_interval_seconds\":10,\"max_interval_seconds\":300,"
            "\"reportable_change\":25,\"capability_flags\":3}",
            &request) == service::ApplicationCommandParseStatus::kOk);
    assert(request.short_addr == 0x2201U);
    // device_id is intentionally left unresolved by the leaf parser (see
    // ReportingProfileWriteRequest) -- the caller (Web/MQTT handler)
    // resolves it via ServiceRuntime before the write.
    assert(!request.profile.key.device_id.valid());
    assert(request.profile.key.endpoint == 1U);
    assert(request.profile.key.cluster_id == 0x0402U);
    assert(request.profile.min_interval_seconds == 10U);
    assert(request.profile.max_interval_seconds == 300U);
    assert(request.profile.reportable_change == 25U);
    assert(request.profile.capability_flags == 3U);

    assert(
        service::parse_web_reporting_profile_request(
            "{\"short_addr\":8705,\"endpoint\":1,\"cluster_id\":1026,"
            "\"min_interval_seconds\":301,\"max_interval_seconds\":300}",
            &request) == service::ApplicationCommandParseStatus::kInvalidProfileBounds);

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
            &request) == service::ApplicationCommandParseStatus::kOk);
    assert(request.short_addr == 0x2201U);
    assert(request.profile.capability_flags == 2U);

    assert(
        service::parse_mqtt_reporting_profile_request(
            "zigbee-gateway/devices/8705/config",
            "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,"
            "\"max_interval_seconds\":300,\"capability_flags\":512}",
            &request) == service::ApplicationCommandParseStatus::kInvalidCapabilityFlags);

    assert(
        service::parse_mqtt_device_power_request(
            "zigbee-gateway/devices/nope/power/set",
            "{\"power_on\":true}",
            &power_request) == service::ApplicationCommandParseStatus::kInvalidTopic);

    // --- Versioned (v1) MQTT command parsers (plan S4 MQTT #10-#13). ---
    constexpr const char* kDeviceIdHex = "00124b0001aa2201";
    char device_id_hex[17]{};
    bool desired_power_on = false;

    assert(
        service::parse_mqtt_v1_device_power_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/power/set",
            "{\"power_on\":true}",
            device_id_hex,
            sizeof(device_id_hex),
            &desired_power_on) == service::ApplicationCommandParseStatus::kOk);
    assert(std::strcmp(device_id_hex, kDeviceIdHex) == 0);
    assert(desired_power_on);

    // Wrong suffix (looks like a config/set topic).
    assert(
        service::parse_mqtt_v1_device_power_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/config/set",
            "{\"power_on\":true}",
            device_id_hex,
            sizeof(device_id_hex),
            &desired_power_on) == service::ApplicationCommandParseStatus::kInvalidTopic);

    // Uppercase hex is rejected -- canonical form is lowercase only.
    assert(
        service::parse_mqtt_v1_device_power_request(
            "zigbee-gateway/v1/devices/00124B0001AA2201/power/set",
            "{\"power_on\":true}",
            device_id_hex,
            sizeof(device_id_hex),
            &desired_power_on) == service::ApplicationCommandParseStatus::kInvalidTopic);

    // Legacy-shaped topic is not accidentally accepted by the v1 parser.
    assert(
        service::parse_mqtt_v1_device_power_request(
            "zigbee-gateway/devices/8705/power/set",
            "{\"power_on\":true}",
            device_id_hex,
            sizeof(device_id_hex),
            &desired_power_on) == service::ApplicationCommandParseStatus::kInvalidTopic);

    // Undersized output buffer.
    char tiny_device_id_hex[8]{};
    assert(
        service::parse_mqtt_v1_device_power_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/power/set",
            "{\"power_on\":true}",
            tiny_device_id_hex,
            sizeof(tiny_device_id_hex),
            &desired_power_on) == service::ApplicationCommandParseStatus::kInvalidTopic);

    // Malformed payload.
    assert(
        service::parse_mqtt_v1_device_power_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/power/set",
            "{\"power_on\":17}",
            device_id_hex,
            sizeof(device_id_hex),
            &desired_power_on) == service::ApplicationCommandParseStatus::kInvalidPayload);

    service::ReportingProfileWriteRequest v1_request{};
    assert(
        service::parse_mqtt_v1_reporting_profile_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/config/set",
            "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,"
            "\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}",
            device_id_hex,
            sizeof(device_id_hex),
            &v1_request) == service::ApplicationCommandParseStatus::kOk);
    assert(std::strcmp(device_id_hex, kDeviceIdHex) == 0);
    // The v1 path never resolves a short_addr at parse time -- the caller
    // (mqtt_bridge) resolves device_id_hex to both a short_addr and a
    // DeviceId via ServiceRuntimeApi after a successful parse.
    assert(v1_request.short_addr == service::kUnknownShortAddr);
    assert(!v1_request.profile.key.device_id.valid());
    assert(v1_request.profile.key.endpoint == 1U);
    assert(v1_request.profile.key.cluster_id == 0x0402U);
    assert(v1_request.profile.min_interval_seconds == 10U);
    assert(v1_request.profile.max_interval_seconds == 300U);
    assert(v1_request.profile.reportable_change == 25U);
    assert(v1_request.profile.capability_flags == 3U);

    assert(
        service::parse_mqtt_v1_reporting_profile_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/config/set",
            "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":301,\"max_interval_seconds\":300}",
            device_id_hex,
            sizeof(device_id_hex),
            &v1_request) == service::ApplicationCommandParseStatus::kInvalidProfileBounds);

    // Wrong suffix (looks like a power/set topic).
    assert(
        service::parse_mqtt_v1_reporting_profile_request(
            "zigbee-gateway/v1/devices/00124b0001aa2201/power/set",
            "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
            device_id_hex,
            sizeof(device_id_hex),
            &v1_request) == service::ApplicationCommandParseStatus::kInvalidTopic);

    return 0;
}
