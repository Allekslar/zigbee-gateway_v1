/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "mqtt_serializer.hpp"

namespace mqtt_bridge {

// Versioned MQTT payload contract (plan S4 MQTT required change #11):
// every v1 payload carries schema_version:1, device_id (where
// applicable), the canonical FD-17 gateway_id and an observation
// timestamp/revision. Deliberately takes gateway_id_hex/device_id_hex as
// plain strings (like mqtt_topics_v1.hpp's builders) rather than naming
// common::GatewayId/a Core-namespaced DeviceId type, so this file stays a
// pure string formatter with no identity-type dependency at all.

constexpr std::size_t kV1GatewayIdHexLength = 12U;

constexpr std::size_t kV1MqttPayloadMaxLen = 448U;

bool serialize_v1_state_payload(
    const char* gateway_id_hex,
    const char* device_id_hex,
    uint32_t revision,
    bool power_on,
    uint32_t observed_at_ms,
    char* out,
    std::size_t out_size,
    std::size_t* out_len) noexcept;

bool serialize_v1_availability_payload(
    const char* gateway_id_hex,
    const char* device_id_hex,
    uint32_t revision,
    bool online,
    uint32_t observed_at_ms,
    char* out,
    std::size_t out_size,
    std::size_t* out_len) noexcept;

// Reuses the legacy MqttSensorSnapshot struct (its fields are payload
// content only, not identity) and adds the v1 identity/schema envelope
// around the same fields the legacy serialize_sensor_payload() emits.
bool serialize_v1_sensor_payload(
    const char* gateway_id_hex,
    const char* device_id_hex,
    uint32_t revision,
    const MqttSensorSnapshot& snapshot,
    char* out,
    std::size_t out_size,
    std::size_t* out_len) noexcept;

bool serialize_v1_gateway_state_payload(
    const char* gateway_id_hex, char* out, std::size_t out_size, std::size_t* out_len) noexcept;

}  // namespace mqtt_bridge
