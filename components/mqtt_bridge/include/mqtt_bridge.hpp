/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "mqtt_discovery.hpp"
#include "service_runtime_api.hpp"
#include "mqtt_serializer.hpp"
#include "mqtt_topics.hpp"

namespace mqtt_bridge {
constexpr std::size_t kMaxMqttPublicationsPerSync = core::kMaxDevices * 3U;

class MqttBridgeTestAccess;

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
    bool handle_command_message(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    std::size_t sync_runtime_snapshot() noexcept;
    std::size_t publish_pending_publications() noexcept;
    std::size_t sync_snapshot(const service::MqttBridgeSnapshot& snapshot) noexcept;
    std::size_t drain_publications(MqttPublishedMessage* out, std::size_t capacity) noexcept;

private:
    friend class MqttBridgeTestAccess;

    bool handle_config_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    bool handle_power_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    void publish_runtime_status() noexcept;
    void set_runtime_status(
        bool enabled,
        bool connected,
        service::NetworkApiSnapshot::MqttConnectionError last_connect_error) noexcept;
    void handle_transport_connected() noexcept;
    void handle_transport_disconnected() noexcept;
    void handle_transport_error(service::NetworkApiSnapshot::MqttConnectionError error) noexcept;
    void handle_transport_subscribe_failure() noexcept;
    void reset_sync_cache() noexcept;
    bool publish_message(const MqttPublishedMessage& message) noexcept;
    bool publish_homeassistant_discovery(const service::MqttBridgeSnapshot& snapshot, bool force_republish) noexcept;
    uint32_t next_command_correlation_id() noexcept;
#ifdef ESP_PLATFORM
    static void task_entry(void* arg) noexcept;
    static void on_transport_connected(void* context) noexcept;
    static void on_transport_disconnected(void* context) noexcept;
    static void on_transport_message(
        void* context,
        const char* topic,
        std::size_t topic_len,
        const uint8_t* payload,
        std::size_t payload_len) noexcept;
    void run_loop() noexcept;
    bool ensure_task_started() noexcept;
    bool start_transport() noexcept;
    bool subscribe_command_topics() noexcept;
#endif

    std::atomic<bool> started_{false};
    std::atomic<uint32_t> next_correlation_id_{1U};
    service::MqttBridgeSnapshot runtime_snapshot_cache_{};
    service::NetworkApiSnapshot::MqttStatusSnapshot runtime_status_cache_{};
    service::MqttBridgeDeviceSnapshot cached_devices_[core::kMaxDevices]{};
    service::MqttBridgeDeviceSnapshot sync_devices_scratch_[core::kMaxDevices]{};
    uint16_t cached_device_count_{0};
    bool cache_initialized_{false};
    MqttPublishedMessage pending_publications_[kMaxMqttPublicationsPerSync]{};
    std::size_t pending_publication_count_{0};
    service::ServiceRuntimeApi* runtime_{nullptr};
    std::atomic<uint32_t> published_message_count_{0};
    std::atomic<bool> transport_enabled_{false};
    std::atomic<bool> command_topics_subscribed_{false};
    bool discovery_republish_requested_{true};
    HomeAssistantDiscoveryMessage discovery_messages_scratch_[kMaxDiscoveryMessagesPerDevice]{};
#ifdef ESP_PLATFORM
    void* task_handle_{nullptr};
#endif
};
void sync_device_state(uint16_t short_addr, bool on) noexcept;

}  // namespace mqtt_bridge
