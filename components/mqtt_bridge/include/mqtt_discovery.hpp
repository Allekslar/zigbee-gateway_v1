/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

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

// Versioned (plan S4 MQTT #16, #19): unique_id/object_id/device
// "identifiers" are derived from both the device's DeviceId and the
// canonical FD-17 GatewayId, and command_topic/state_topic/
// availability_topic all point at the v1 topic tree. gateway_id_hex must
// be exactly 12 lowercase hex characters (kV1GatewayIdHexLength);
// device.device_id_hex must be exactly 16 lowercase hex characters --
// a device with no resolved identity yet produces no messages (there is
// nothing DeviceId-keyed to publish).
std::size_t build_homeassistant_discovery_messages(
    const char* gateway_id_hex,
    const service::MqttBridgeDeviceSnapshot& device,
    HomeAssistantDiscoveryMessage* out,
    std::size_t capacity) noexcept;

bool discovery_schema_changed(
    const service::MqttBridgeDeviceSnapshot& previous,
    const service::MqttBridgeDeviceSnapshot& current) noexcept;

// One-time legacy cleanup (plan S4 MQTT #14/#17): builds retained
// empty-payload tombstones for all 5 possible legacy short_addr-keyed
// discovery topics of a device, unconditionally -- callers cannot know in
// general which of the 5 entity types a device actually had published
// under the legacy scheme, so every possible one is tombstoned; tombstoning
// a topic that was never retained is a harmless no-op.
std::size_t build_legacy_homeassistant_discovery_tombstones(
    uint16_t short_addr,
    HomeAssistantDiscoveryMessage* out,
    std::size_t capacity) noexcept;

}  // namespace mqtt_bridge
