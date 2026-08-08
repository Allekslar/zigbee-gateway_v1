/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace mqtt_bridge {

// Versioned MQTT topic contract (plan S4 MQTT required changes #10, #12).
// Canonical topic root stays "zigbee-gateway" (same constant the legacy
// topics in mqtt_topics.hpp use; "configurable" per the plan means this
// single named root, not a new runtime-configurable mechanism -- the
// legacy topics have never had one either), with a "/v1/" segment and the
// device key changed from short_addr to DeviceId:
//   zigbee-gateway/v1/devices/<device_id>/state
//   zigbee-gateway/v1/devices/<device_id>/telemetry
//   zigbee-gateway/v1/devices/<device_id>/availability
//   zigbee-gateway/v1/devices/<device_id>/config/set
//   zigbee-gateway/v1/devices/<device_id>/power/set
//   zigbee-gateway/v1/devices/<device_id>/commands/<operation_id>/result
//   zigbee-gateway/v1/gateway/state
//
// This file is purely additive: it does not replace or call into
// mqtt_topics.hpp, and nothing in mqtt_bridge.cpp's live publish/subscribe
// path calls these functions yet -- see docs/architecture/MQTT_API_V1.md
// for why the actual production cutover (plan #13-#15) is a separate,
// deliberately deferred step from this topic/schema foundation.

// Canonical DeviceId text length (matches the Core layer's device-id hex
// length constant, redefined here rather than included so this adapter
// layer never has to name a Core-namespaced symbol -- INV-M026).
constexpr std::size_t kV1DeviceIdHexLength = 16U;

constexpr std::size_t kV1TopicMaxLen = 96U;

// Builders take an already-canonical, already-validated device_id_hex
// string (exactly kV1DeviceIdHexLength lowercase hex characters plus a
// null terminator) -- callers are expected to have obtained it from a
// trusted source (e.g. Service's own device_id_hex snapshot field), not
// from untrusted external input. Untrusted device_id text (e.g. parsed out
// of an inbound MQTT topic) must go through parse_v1_device_id_from_topic()
// below instead, which performs full validation.
bool topic_v1_device_state(const char* device_id_hex, char* out, std::size_t out_size) noexcept;
bool topic_v1_device_telemetry(const char* device_id_hex, char* out, std::size_t out_size) noexcept;
bool topic_v1_device_availability(const char* device_id_hex, char* out, std::size_t out_size) noexcept;
bool topic_v1_device_config_set(const char* device_id_hex, char* out, std::size_t out_size) noexcept;
const char* topic_v1_device_config_set_wildcard() noexcept;
bool topic_v1_device_power_set(const char* device_id_hex, char* out, std::size_t out_size) noexcept;
const char* topic_v1_device_power_set_wildcard() noexcept;
bool topic_v1_device_command_result(
    const char* device_id_hex, uint32_t operation_id, char* out, std::size_t out_size) noexcept;
const char* topic_v1_gateway_state() noexcept;

// Parses "<prefix><16 lowercase hex chars><suffix>" out of an inbound MQTT
// topic string (mirrors web_ui::extract_uri_device_id_hex()'s contract
// exactly -- MQTT topic segments and URL path segments share the same
// slash-delimited shape). Rejects wrong length, wrong case and any
// malformed character in the device_id segment (plan #12); prefix/suffix
// must match exactly.
bool parse_v1_device_id_from_topic(
    const char* topic, const char* prefix, const char* suffix, char* out_hex, std::size_t out_hex_capacity) noexcept;

}  // namespace mqtt_bridge
