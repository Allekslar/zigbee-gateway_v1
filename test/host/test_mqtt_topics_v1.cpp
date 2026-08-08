/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "mqtt_topics_v1.hpp"

int main() {
    using namespace mqtt_bridge;

    constexpr const char* kDeviceIdHex = "00124b0001aa2201";

    char topic[kV1TopicMaxLen] = {0};

    assert(topic_v1_device_state(kDeviceIdHex, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/v1/devices/00124b0001aa2201/state") == 0);

    assert(topic_v1_device_telemetry(kDeviceIdHex, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/v1/devices/00124b0001aa2201/telemetry") == 0);

    assert(topic_v1_device_availability(kDeviceIdHex, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/v1/devices/00124b0001aa2201/availability") == 0);

    assert(topic_v1_device_config_set(kDeviceIdHex, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/v1/devices/00124b0001aa2201/config/set") == 0);
    assert(std::strcmp(topic_v1_device_config_set_wildcard(), "zigbee-gateway/v1/devices/+/config/set") == 0);

    assert(topic_v1_device_power_set(kDeviceIdHex, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/v1/devices/00124b0001aa2201/power/set") == 0);
    assert(std::strcmp(topic_v1_device_power_set_wildcard(), "zigbee-gateway/v1/devices/+/power/set") == 0);

    assert(topic_v1_device_command_result(kDeviceIdHex, 42U, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/v1/devices/00124b0001aa2201/commands/42/result") == 0);

    assert(std::strcmp(topic_v1_gateway_state(), "zigbee-gateway/v1/gateway/state") == 0);

    // --- Builder rejects malformed/wrong-length/null device_id_hex and undersized buffers. ---
    char tiny[8] = {0};
    assert(!topic_v1_device_telemetry(kDeviceIdHex, tiny, sizeof(tiny)));
    assert(!topic_v1_device_state(kDeviceIdHex, nullptr, sizeof(topic)));
    assert(!topic_v1_device_state(kDeviceIdHex, topic, 0U));
    assert(!topic_v1_device_state(nullptr, topic, sizeof(topic)));
    assert(!topic_v1_device_state("00124b0001aa22", topic, sizeof(topic)));      // too short (14 chars)
    assert(!topic_v1_device_state("00124b0001aa22011", topic, sizeof(topic)));   // too long (17 chars)
    assert(!topic_v1_device_command_result(kDeviceIdHex, 42U, nullptr, sizeof(topic)));

    // --- parse_v1_device_id_from_topic(): well-formed round-trip. ---
    char out_hex[kV1DeviceIdHexLength + 1U] = {};
    assert(parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa2201/power/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    assert(std::strcmp(out_hex, kDeviceIdHex) == 0);

    assert(parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa2201/config/set", "zigbee-gateway/v1/devices/", "/config/set", out_hex,
        sizeof(out_hex)));
    assert(std::strcmp(out_hex, kDeviceIdHex) == 0);

    // No suffix ("" means the device_id is the final path segment).
    assert(parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa2201", "zigbee-gateway/v1/devices/", "", out_hex, sizeof(out_hex)));
    assert(std::strcmp(out_hex, kDeviceIdHex) == 0);

    // --- Malformed inputs are rejected (plan #12). ---
    // Wrong length (short).
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa22/power/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    // Wrong length (long).
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa220199/power/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    // Uppercase hex is rejected -- canonical form is lowercase only.
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124B0001AA2201/power/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    // Non-hex character.
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa220g/power/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    // Wrong prefix.
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/devices/00124b0001aa2201/power/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    // Wrong suffix.
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa2201/config/set", "zigbee-gateway/v1/devices/", "/power/set", out_hex,
        sizeof(out_hex)));
    // Null args / undersized output buffer.
    assert(!parse_v1_device_id_from_topic(nullptr, "zigbee-gateway/v1/devices/", "/power/set", out_hex, sizeof(out_hex)));
    char tiny_out[4] = {};
    assert(!parse_v1_device_id_from_topic(
        "zigbee-gateway/v1/devices/00124b0001aa2201/power/set", "zigbee-gateway/v1/devices/", "/power/set", tiny_out,
        sizeof(tiny_out)));

    return 0;
}
