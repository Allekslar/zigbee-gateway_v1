/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace mqtt_bridge {

// Stable MQTT topic contract (Phase 2):
//   zigbee-gateway/devices
//   zigbee-gateway/state
//   zigbee-gateway/devices/<short_addr>/state
//   zigbee-gateway/devices/<short_addr>/telemetry
//   zigbee-gateway/devices/<short_addr>/availability
//   zigbee-gateway/devices/<short_addr>/config

constexpr std::size_t kTopicMaxLen = 64U;

const char* topic_devices() noexcept;
const char* topic_state() noexcept;

bool topic_device_state(uint16_t short_addr, char* out, std::size_t out_size) noexcept;
bool topic_device_telemetry(uint16_t short_addr, char* out, std::size_t out_size) noexcept;
bool topic_device_availability(uint16_t short_addr, char* out, std::size_t out_size) noexcept;
bool topic_device_config(uint16_t short_addr, char* out, std::size_t out_size) noexcept;
const char* topic_device_config_wildcard() noexcept;

}  // namespace mqtt_bridge
