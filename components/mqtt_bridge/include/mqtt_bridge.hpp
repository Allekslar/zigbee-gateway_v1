/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "core_state.hpp"
#include "mqtt_serializer.hpp"
#include "mqtt_topics.hpp"

namespace mqtt_bridge {

constexpr std::size_t kMaxMqttPublicationsPerSync = core::kMaxDevices * 3U;

struct MqttPublishedMessage {
    char topic[kTopicMaxLen]{};
    char payload[kMqttPayloadMaxLen]{};
    bool retain{false};
};

class MqttBridge {
public:
    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;
    std::size_t sync_snapshot(const core::CoreState& state) noexcept;
    std::size_t drain_publications(MqttPublishedMessage* out, std::size_t capacity) noexcept;

private:
    void reset_sync_cache() noexcept;

    bool started_{false};
    core::CoreDeviceRecord cached_devices_[core::kMaxDevices]{};
    uint16_t cached_device_count_{0};
    bool cache_initialized_{false};
    MqttPublishedMessage pending_publications_[kMaxMqttPublicationsPerSync]{};
    std::size_t pending_publication_count_{0};
};

void publish_discovery() noexcept;
void sync_device_state(uint16_t short_addr, bool on) noexcept;

}  // namespace mqtt_bridge
