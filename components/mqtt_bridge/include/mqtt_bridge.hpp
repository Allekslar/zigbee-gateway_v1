/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "mqtt_discovery.hpp"
#include "mqtt_serializer.hpp"
#include "mqtt_serializer_v1.hpp"
#include "mqtt_topics.hpp"
#include "mqtt_topics_v1.hpp"
#include "runtime_lock.hpp"
#include "service_runtime_api.hpp"

namespace mqtt_bridge {
// Deliberately NOT the theoretical worst case (kServiceMaxDevices * 3 == 192,
// i.e. every device publishing three topics on one pass). At 452 bytes per
// entry that would be ~85KB, which real HIL showed this device cannot afford:
// as a plain .bss array it starved the Wi-Fi driver during association, and
// even once moved to a lifecycle-scoped allocation it then starved
// mbedtls_ssl_setup() of the memory a TLS session needs. 48 entries is ~21KB.
// Overflow is a normal, expected condition at this size rather than an error:
// sync_snapshot() detects it and withholds its cache commit, so the next pass
// republishes whatever was dropped (see that function's own commit block).
// The cost of a burst larger than the queue is therefore extra latency, not a
// lost publication.
constexpr std::size_t kMaxMqttPublicationsPerSync = 48U;
constexpr uint32_t kMqttPowerOverrideWindowMs = 15000U;

class MqttBridgeTestAccess;

struct MqttPublishedMessage {
    char topic[kTopicMaxLen]{};
    char payload[kMqttPayloadMaxLen]{};
    bool retain{false};
};

struct PendingPowerOverride {
    uint16_t short_addr{service::kUnknownShortAddr};
    bool power_on{false};
    bool active{false};
    uint32_t expires_at_ms{0};
};

class MqttBridge {
public:
    MqttBridge() noexcept = default;
    // pending_publications_ is an owned heap allocation (see the member's
    // own comment), so this class needs a destructor to be correct: an
    // instance destroyed without a preceding stop() would otherwise leak
    // the queue. On the device that never happens -- the bridge is a
    // global that outlives the process -- but host tests construct and
    // destroy bridges freely, and LeakSanitizer is right to object.
    ~MqttBridge() noexcept { release_publication_queue(); }
    // Owning a raw pointer means the compiler-generated copy/move would
    // double-free. The atomic members already make this class
    // non-copyable, but state it explicitly rather than relying on that.
    MqttBridge(const MqttBridge&) = delete;
    MqttBridge& operator=(const MqttBridge&) = delete;
    MqttBridge(MqttBridge&&) = delete;
    MqttBridge& operator=(MqttBridge&&) = delete;

    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;
    void attach_runtime(service::ServiceRuntimeApi* runtime) noexcept;
    bool handle_command_message(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    std::size_t sync_runtime_snapshot() noexcept;
    std::size_t publish_pending_publications() noexcept;
    std::size_t sync_snapshot(const service::MqttBridgeSnapshot& snapshot) noexcept;
    std::size_t drain_publications(MqttPublishedMessage* out, std::size_t capacity) noexcept;
    // Allocates pending_publications_ on first need and reports whether the
    // queue is usable. Safe to call repeatedly; see the member's own comment
    // for why this queue is not a plain .bss array.
    bool ensure_publication_queue() noexcept;
    void release_publication_queue() noexcept;

private:
    friend class MqttBridgeTestAccess;

    bool handle_config_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    bool handle_power_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    bool handle_config_command_v1(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
    bool handle_power_command_v1(const char* topic, const char* payload, uint32_t correlation_id) noexcept;
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
    void set_power_override(uint16_t short_addr, bool on) noexcept;
    void apply_power_overrides(service::MqttBridgeSnapshot* snapshot) noexcept;
    bool publish_message(const MqttPublishedMessage& message) noexcept;
    bool publish_homeassistant_discovery(const service::MqttBridgeSnapshot& snapshot, bool force_republish) noexcept;
    void sync_device_state(uint16_t short_addr, bool on) noexcept;
    uint32_t next_command_correlation_id() noexcept;
    // Formats the canonical FD-17 gateway_id into gateway_id_hex_ once
    // (stable for the process lifetime) the first time it is needed by v1
    // publishing; a no-op once already populated.
    void ensure_gateway_id_hex() noexcept;
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
    service::MqttBridgeDeviceSnapshot cached_devices_[service::kServiceMaxDevices]{};
    service::MqttBridgeDeviceSnapshot sync_devices_scratch_[service::kServiceMaxDevices]{};
    uint16_t cached_device_count_{0};
    bool cache_initialized_{false};
    // Lifecycle-scoped, NOT permanently reserved .bss. At
    // kMaxMqttPublicationsPerSync (192) x sizeof(MqttPublishedMessage) (452)
    // this queue is ~85KB -- as a plain member array it made the whole
    // MqttBridge object ~99KB of .bss, permanently reserved whether or not
    // MQTT was ever used. Real HIL testing found that this single array is
    // what starved the Wi-Fi driver of DMA-capable heap on the production +
    // Flash-Encryption profile: only ~476 bytes remained free during
    // association, so the driver could not allocate a buffer for its own
    // management frames and every connection attempt failed (the driver's
    // "m f auth"/"m f assoc req"/"alloc eb ... fail" messages are exactly
    // that allocation failing). Allocated on demand instead -- in start(),
    // i.e. only once the network is already up, which is precisely when the
    // Wi-Fi association buffers are no longer needed -- and released in
    // stop(). Capacity and drop-when-full semantics are unchanged; a failed
    // allocation simply leaves the capacity at 0, which the existing
    // capacity guards already treat as "queue full" (publications are
    // skipped, the same graceful degradation this bridge already applies to
    // any other publication-build failure).
    MqttPublishedMessage* pending_publications_{nullptr};
    std::size_t pending_publication_capacity_{0};
    std::size_t pending_publication_count_{0};
    service::ServiceRuntimeApi* runtime_{nullptr};
    std::atomic<uint32_t> published_message_count_{0};
    std::atomic<bool> transport_enabled_{false};
    std::atomic<bool> command_topics_subscribed_{false};
    bool discovery_republish_requested_{true};
    HomeAssistantDiscoveryMessage discovery_messages_scratch_[kMaxDiscoveryMessagesPerDevice]{};
    PendingPowerOverride power_overrides_[service::kServiceMaxDevices]{};
    mutable service::RuntimeLock state_lock_{};
    char gateway_id_hex_[kV1GatewayIdHexLength + 1U]{};
    bool gateway_id_hex_ready_{false};
    bool legacy_discovery_tombstoned_{false};
#ifdef ESP_PLATFORM
    void* task_handle_{nullptr};
#endif
};

}  // namespace mqtt_bridge
