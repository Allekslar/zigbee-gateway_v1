/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>

#include "service_runtime_api.hpp"

namespace mqtt_bridge {

constexpr std::size_t kDiscoveryTopicMaxLen = 128U;
constexpr std::size_t kDiscoveryPayloadMaxLen = 768U;
constexpr std::size_t kMaxDiscoveryMessagesPerDevice = 7U;

struct HomeAssistantDiscoveryMessage {
    char topic[kDiscoveryTopicMaxLen]{};
    char payload[kDiscoveryPayloadMaxLen]{};
    bool retain{true};
};

std::size_t build_homeassistant_discovery_messages(
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out,
    std::size_t capacity) noexcept;

bool discovery_schema_changed(
    const service::MqttBridgeDeviceSnapshot& previous,
    const service::MqttBridgeDeviceSnapshot& current) noexcept;

}  // namespace mqtt_bridge
