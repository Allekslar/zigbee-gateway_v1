/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core_state.hpp"
#include "mqtt_serializer.hpp"
#include "mqtt_topics.hpp"

namespace service {
class ServiceRuntimeApi;
}

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
    void attach_runtime(service::ServiceRuntimeApi* runtime) noexcept;
    bool handle_config_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    std::size_t sync_runtime_snapshot() noexcept;
    std::size_t publish_pending_publications() noexcept;
    std::size_t sync_snapshot(const core::CoreState& state) noexcept;
    std::size_t drain_publications(MqttPublishedMessage* out, std::size_t capacity) noexcept;

private:
    void reset_sync_cache() noexcept;
    bool publish_message(const MqttPublishedMessage& message) noexcept;
#ifdef ESP_PLATFORM
    static void task_entry(void* arg) noexcept;
    void run_loop() noexcept;
    bool ensure_task_started() noexcept;
#endif

    std::atomic<bool> started_{false};
    core::CoreDeviceRecord cached_devices_[core::kMaxDevices]{};
    uint16_t cached_device_count_{0};
    bool cache_initialized_{false};
    MqttPublishedMessage pending_publications_[kMaxMqttPublicationsPerSync]{};
    std::size_t pending_publication_count_{0};
    service::ServiceRuntimeApi* runtime_{nullptr};
    std::atomic<uint32_t> published_message_count_{0};
#ifdef ESP_PLATFORM
    void* task_handle_{nullptr};
#endif
};

void publish_discovery() noexcept;
void sync_device_state(uint16_t short_addr, bool on) noexcept;

}  // namespace mqtt_bridge
