/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_bridge.hpp"

#include <chrono>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include "hal_mqtt.h"
#include "sdkconfig.h"
#endif
#include "application_command_mapper.hpp"
#include "gateway_id.hpp"
#include "log_tags.h"
#include "mqtt_discovery.hpp"
#include "service_runtime_api.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace mqtt_bridge {
namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_MQTT_BRIDGE;
hal_mqtt_config_t build_transport_config() noexcept {
    hal_mqtt_config_t config{};
    config.broker_uri = CONFIG_ZGW_MQTT_BROKER_URI;
    config.client_id = CONFIG_ZGW_MQTT_CLIENT_ID;
#if defined(CONFIG_ZGW_MQTT_USERNAME)
    config.username = CONFIG_ZGW_MQTT_USERNAME;
#endif
#if defined(CONFIG_ZGW_MQTT_PASSWORD)
    config.password = CONFIG_ZGW_MQTT_PASSWORD;
#endif
    config.keepalive_sec = CONFIG_ZGW_MQTT_KEEPALIVE_SEC;
    config.network_timeout_ms = CONFIG_ZGW_MQTT_NETWORK_TIMEOUT_MS;
    config.reconnect_timeout_ms = CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS;
    config.auto_reconnect = true;
    return config;
}

constexpr const char* kMqttBridgeTaskName = "mqtt_bridge";
constexpr uint32_t kMqttBridgeTaskStackSize = 6144U;
constexpr UBaseType_t kMqttBridgeTaskPriority = 4U;
constexpr TickType_t kMqttBridgeTaskPeriodTicks = pdMS_TO_TICKS(1000);
#endif
constexpr std::size_t kDrainBatchCapacity = 8U;
constexpr int kMqttQosAtLeastOnce = 1;

using MqttConnectionError = service::NetworkApiSnapshot::MqttConnectionError;

uint32_t monotonic_now_ms() noexcept {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

// Identity-keyed (plan S4 MQTT #15's remap-safety principle, applied here
// too): a short_addr remap must not look like "new device" for discovery
// schema-change detection, or it would trigger a spurious (harmless but
// wasteful) discovery republish.
const service::MqttBridgeDeviceSnapshot* find_cached_device_by_device_id_hex(
    const service::MqttBridgeDeviceSnapshot* devices,
    const uint16_t count,
    const char* device_id_hex) noexcept {
    if (devices == nullptr || device_id_hex == nullptr || device_id_hex[0] == '\0') {
        return nullptr;
    }
    for (uint16_t i = 0; i < count; ++i) {
        if (devices[i].device_id_hex[0] != '\0' && std::strcmp(devices[i].device_id_hex.data(), device_id_hex) == 0) {
            return &devices[i];
        }
    }
    return nullptr;
}

}  // namespace

bool MqttBridge::ensure_publication_queue() noexcept {
    if (pending_publications_ != nullptr) {
        return true;
    }

    void* const buffer = calloc(kMaxMqttPublicationsPerSync, sizeof(MqttPublishedMessage));
    if (buffer == nullptr) {
        return false;
    }

    pending_publications_ = static_cast<MqttPublishedMessage*>(buffer);
    pending_publication_capacity_ = kMaxMqttPublicationsPerSync;
    pending_publication_count_ = 0U;
    return true;
}

void MqttBridge::release_publication_queue() noexcept {
    if (pending_publications_ == nullptr) {
        return;
    }

    free(pending_publications_);
    pending_publications_ = nullptr;
    pending_publication_capacity_ = 0U;
    pending_publication_count_ = 0U;
}

bool MqttBridge::start() noexcept {
    // Claim the publication queue here rather than holding it in .bss for
    // the device's whole uptime -- start() runs only once the network is
    // already up, so this never competes with Wi-Fi association for
    // internal SRAM (see the member's own comment for the real HIL finding).
    // A failed allocation is not fatal: the capacity guards degrade to
    // "skip publications", same as any other publication-build failure.
    (void)ensure_publication_queue();
    reset_sync_cache();
#ifdef ESP_PLATFORM
    transport_enabled_.store(start_transport(), std::memory_order_release);
#else
    transport_enabled_.store(true, std::memory_order_release);
    set_runtime_status(true, true, MqttConnectionError::kNone);
#endif
    started_.store(true, std::memory_order_release);
    publish_runtime_status();
#ifdef ESP_PLATFORM
    return ensure_task_started();
#else
    return started();
#endif
}

void MqttBridge::stop() noexcept {
    reset_sync_cache();
    transport_enabled_.store(false, std::memory_order_release);
    started_.store(false, std::memory_order_release);
    set_runtime_status(runtime_status_cache_.enabled, false, runtime_status_cache_.last_connect_error);
    publish_runtime_status();
#ifdef ESP_PLATFORM
    for (uint8_t i = 0; i < 20U && task_handle_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif
    // Deliberately last: the worker task above is the only other writer to
    // this queue, so releasing it before that task has actually exited
    // would race. Anything still staged at this point is discarded by
    // design -- stop() already resets the sync cache, so the next start()
    // republishes current state from scratch anyway.
    release_publication_queue();
}

bool MqttBridge::started() const noexcept {
    return started_.load(std::memory_order_acquire);
}

void MqttBridge::attach_runtime(service::ServiceRuntimeApi* runtime) noexcept {
    runtime_ = runtime;
    ensure_gateway_id_hex();
    publish_runtime_status();
#ifdef ESP_PLATFORM
    (void)ensure_task_started();
#endif
}

bool MqttBridge::handle_command_message(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (topic == nullptr || payload == nullptr) {
        return false;
    }

    constexpr const char* kV1DevicesPrefix = "zigbee-gateway/v1/devices/";
    if (std::strncmp(topic, kV1DevicesPrefix, std::strlen(kV1DevicesPrefix)) == 0) {
        if (service::mqtt_topic_has_suffix(topic, "/config/set")) {
            return handle_config_command_v1(topic, payload, correlation_id);
        }
        if (service::mqtt_topic_has_suffix(topic, "/power/set")) {
            return handle_power_command_v1(topic, payload, correlation_id);
        }
        return false;
    }

    // Legacy short-address command topics: kept reachable only for host
    // tests and any explicit dev-profile use. subscribe_command_topics()
    // no longer subscribes to these wildcards in production (plan S4 MQTT
    // #13), so a real broker never delivers a legacy-shaped topic here.
    if (service::mqtt_topic_has_suffix(topic, "/config")) {
        return handle_config_command(topic, payload, correlation_id);
    }
    if (service::mqtt_topic_has_suffix(topic, "/power/set")) {
        return handle_power_command(topic, payload, correlation_id);
    }

    return false;
}

bool MqttBridge::handle_config_command_v1(
    const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (runtime_ == nullptr || correlation_id == service::kNoCorrelationId) {
        return false;
    }

    char device_id_hex[kV1DeviceIdHexLength + 1U]{};
    service::ReportingProfileWriteRequest request{};
    if (service::parse_mqtt_v1_reporting_profile_request(
            topic, payload, device_id_hex, sizeof(device_id_hex), &request) !=
        service::ApplicationCommandParseStatus::kOk) {
        return false;
    }

    uint16_t short_addr = 0U;
    if (runtime_->resolve_short_addr_for_device_id_hex(device_id_hex, &short_addr) !=
        service::ServiceRuntimeApi::DeviceIdResolveStatus::kResolved) {
        return false;
    }

    // Deliberately `auto`, not a spelled-out Core-namespaced type: MQTT
    // bridge adapters must not name Core symbols directly (INV-M026) --
    // mirrors handle_config_command()'s exact pattern.
    const auto device_id = runtime_->resolve_device_id_for_short_addr(short_addr);
    if (!device_id.valid()) {
        return false;
    }
    request.profile.key.device_id = device_id;

    return runtime_->post_reporting_profile_write(request.profile);
}

bool MqttBridge::handle_power_command_v1(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (runtime_ == nullptr || correlation_id == service::kNoCorrelationId) {
        return false;
    }

    char device_id_hex[kV1DeviceIdHexLength + 1U]{};
    bool desired_power_on = false;
    if (service::parse_mqtt_v1_device_power_request(
            topic, payload, device_id_hex, sizeof(device_id_hex), &desired_power_on) !=
        service::ApplicationCommandParseStatus::kOk) {
        return false;
    }

    uint16_t short_addr = 0U;
    if (runtime_->resolve_short_addr_for_device_id_hex(device_id_hex, &short_addr) !=
        service::ServiceRuntimeApi::DeviceIdResolveStatus::kResolved) {
        return false;
    }

    service::DevicePowerCommandRequest request{};
    request.correlation_id = correlation_id;
    request.short_addr = short_addr;
    request.desired_power_on = desired_power_on;
    request.issued_at_ms = monotonic_now_ms();
    if (runtime_->post_device_power_request(request) != service::CommandSubmitStatus::kAccepted) {
        return false;
    }

    {
        service::RuntimeLockGuard guard(state_lock_);
        set_power_override(short_addr, desired_power_on);
        sync_device_state(short_addr, desired_power_on);
    }
    return true;
}

bool MqttBridge::handle_config_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (runtime_ == nullptr || correlation_id == service::kNoCorrelationId) {
        return false;
    }

    service::ReportingProfileWriteRequest request{};
    if (service::parse_mqtt_reporting_profile_request(topic, payload, &request) !=
        service::ApplicationCommandParseStatus::kOk) {
        return false;
    }

    // Deliberately `auto`, not a spelled-out Core-namespaced type: MQTT
    // bridge adapters must not name Core symbols directly (INV-M026) -- the
    // DeviceId type stays opaque to this layer, which only needs .valid()
    // and to store it back into the service-owned ReportingProfileKey.
    const auto device_id = runtime_->resolve_device_id_for_short_addr(request.short_addr);
    if (!device_id.valid()) {
        return false;
    }
    request.profile.key.device_id = device_id;

    return runtime_->post_reporting_profile_write(request.profile);
}

bool MqttBridge::handle_power_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (runtime_ == nullptr || correlation_id == service::kNoCorrelationId) {
        return false;
    }

    service::DevicePowerCommandRequest request{};
    if (service::parse_mqtt_device_power_request(topic, payload, &request) !=
        service::ApplicationCommandParseStatus::kOk) {
        return false;
    }

    request.correlation_id = correlation_id;
    request.issued_at_ms = monotonic_now_ms();
    if (runtime_->post_device_power_request(request) != service::CommandSubmitStatus::kAccepted) {
        return false;
    }

    {
        service::RuntimeLockGuard guard(state_lock_);
        set_power_override(request.short_addr, request.desired_power_on);
        sync_device_state(request.short_addr, request.desired_power_on);
    }
    return true;
}

std::size_t MqttBridge::sync_runtime_snapshot() noexcept {
    if (!started() || runtime_ == nullptr || !transport_enabled_.load(std::memory_order_acquire)) {
        return 0U;
    }

    if (!runtime_->build_mqtt_bridge_snapshot(&runtime_snapshot_cache_)) {
        return 0U;
    }

    service::RuntimeLockGuard guard(state_lock_);
    apply_power_overrides(&runtime_snapshot_cache_);

    (void)publish_homeassistant_discovery(
        runtime_snapshot_cache_,
        discovery_republish_requested_);
    return sync_snapshot(runtime_snapshot_cache_);
}

std::size_t MqttBridge::publish_pending_publications() noexcept {
    if (!transport_enabled_.load(std::memory_order_acquire)) {
        service::RuntimeLockGuard guard(state_lock_);
        pending_publication_count_ = 0U;
        return 0U;
    }

    MqttPublishedMessage batch[kDrainBatchCapacity]{};
    std::size_t published = 0U;

    while (true) {
        std::size_t drained = 0U;
        {
            service::RuntimeLockGuard guard(state_lock_);
            drained = drain_publications(batch, kDrainBatchCapacity);
        }
        if (drained == 0U) {
            break;
        }

        for (std::size_t i = 0; i < drained; ++i) {
            if (publish_message(batch[i])) {
                ++published;
            }
        }
    }

    published_message_count_.fetch_add(static_cast<uint32_t>(published), std::memory_order_relaxed);
    return published;
}

void MqttBridge::sync_device_state(const uint16_t short_addr, const bool on) noexcept {
    if (!cache_initialized_) {
        return;
    }

    for (uint16_t i = 0; i < cached_device_count_; ++i) {
        service::MqttBridgeDeviceSnapshot& device = cached_devices_[i];
        if (device.short_addr != short_addr) {
            continue;
        }
        if (device.power_on == on) {
            return;
        }

        device.power_on = on;
        // v1-only: this optimistic post-command update targets the
        // DeviceId-keyed v1 state topic exclusively (plan S4 MQTT #13/#15
        // -- legacy topics are never written with live content after
        // cutover, only tombstoned once). A device without a resolved
        // identity yet cannot be published under a v1 topic; it is simply
        // skipped here, matching the pre-existing silent-skip convention
        // for any other publication-build failure in this function.
        if (device.device_id_hex[0] == '\0') {
            return;
        }
        ensure_gateway_id_hex();
        if (!gateway_id_hex_ready_) {
            return;
        }

        MqttPublishedMessage publication{};
        if (!topic_v1_device_state(device.device_id_hex.data(), publication.topic, sizeof(publication.topic))) {
            return;
        }
        std::size_t payload_len = 0U;
        if (!serialize_v1_state_payload(
                gateway_id_hex_,
                device.device_id_hex.data(),
                runtime_snapshot_cache_.revision,
                device.power_on,
                monotonic_now_ms(),
                publication.payload,
                sizeof(publication.payload),
                &payload_len) ||
            payload_len == 0U) {
            return;
        }
        publication.retain = true;
        if (!ensure_publication_queue() || pending_publication_count_ >= pending_publication_capacity_) {
            return;
        }
        pending_publications_[pending_publication_count_++] = publication;
        return;
    }
}

void MqttBridge::ensure_gateway_id_hex() noexcept {
    if (gateway_id_hex_ready_ || runtime_ == nullptr) {
        return;
    }

    const common::GatewayId gateway_id = runtime_->gateway_id();
    if (!gateway_id.valid() || !gateway_id.format(gateway_id_hex_, sizeof(gateway_id_hex_))) {
        return;
    }
    gateway_id_hex_[kV1GatewayIdHexLength] = '\0';
    gateway_id_hex_ready_ = true;
}

void MqttBridge::publish_runtime_status() noexcept {
    if (runtime_ == nullptr) {
        return;
    }
    (void)runtime_->post_mqtt_status(runtime_status_cache_);
}

void MqttBridge::set_runtime_status(
    const bool enabled,
    const bool connected,
    const MqttConnectionError last_connect_error) noexcept {
    runtime_status_cache_.enabled = enabled;
    runtime_status_cache_.connected = connected;
    runtime_status_cache_.last_connect_error = last_connect_error;
}

void MqttBridge::handle_transport_connected() noexcept {
    service::RuntimeLockGuard guard(state_lock_);
    set_runtime_status(true, true, MqttConnectionError::kNone);
    discovery_republish_requested_ = true;
    publish_runtime_status();
}

void MqttBridge::handle_transport_disconnected() noexcept {
    service::RuntimeLockGuard guard(state_lock_);
    command_topics_subscribed_.store(false, std::memory_order_release);
    set_runtime_status(true, false, runtime_status_cache_.last_connect_error);
    publish_runtime_status();
}

void MqttBridge::handle_transport_error(const MqttConnectionError error) noexcept {
    service::RuntimeLockGuard guard(state_lock_);
    set_runtime_status(true, false, error);
    publish_runtime_status();
}

void MqttBridge::handle_transport_subscribe_failure() noexcept {
    service::RuntimeLockGuard guard(state_lock_);
    set_runtime_status(true, false, MqttConnectionError::kSubscribeFailed);
    publish_runtime_status();
}

void MqttBridge::reset_sync_cache() noexcept {
    service::RuntimeLockGuard guard(state_lock_);
    cached_device_count_ = 0;
    cache_initialized_ = false;
    legacy_discovery_tombstoned_ = false;
    pending_publication_count_ = 0;
    command_topics_subscribed_.store(false, std::memory_order_release);
    discovery_republish_requested_ = true;
    for (auto& entry : power_overrides_) {
        entry = PendingPowerOverride{};
    }
}

void MqttBridge::set_power_override(const uint16_t short_addr, const bool on) noexcept {
    const uint32_t expires_at_ms = monotonic_now_ms() + kMqttPowerOverrideWindowMs;

    for (auto& entry : power_overrides_) {
        if (entry.active && entry.short_addr == short_addr) {
            entry.power_on = on;
            entry.expires_at_ms = expires_at_ms;
            return;
        }
    }

    for (auto& entry : power_overrides_) {
        if (entry.active) {
            continue;
        }
        entry.short_addr = short_addr;
        entry.power_on = on;
        entry.active = true;
        entry.expires_at_ms = expires_at_ms;
        return;
    }
}

void MqttBridge::apply_power_overrides(service::MqttBridgeSnapshot* snapshot) noexcept {
    if (snapshot == nullptr) {
        return;
    }

    const uint32_t now_ms = monotonic_now_ms();
    for (auto& override_entry : power_overrides_) {
        if (!override_entry.active) {
            continue;
        }
        if (now_ms >= override_entry.expires_at_ms) {
            override_entry = PendingPowerOverride{};
            continue;
        }

        for (std::size_t i = 0; i < snapshot->device_count && i < snapshot->devices.size(); ++i) {
            service::MqttBridgeDeviceSnapshot& device = snapshot->devices[i];
            if (device.short_addr != override_entry.short_addr || !device.online) {
                continue;
            }

            if (device.power_on == override_entry.power_on) {
                override_entry = PendingPowerOverride{};
            } else {
                device.power_on = override_entry.power_on;
            }
            break;
        }
    }
}

bool MqttBridge::publish_message(const MqttPublishedMessage& message) noexcept {
#ifdef ESP_PLATFORM
    return hal_mqtt_publish(message.topic, message.payload, message.retain, kMqttQosAtLeastOnce) == HAL_MQTT_STATUS_OK;
#else
    (void)message;
    return true;
#endif
}

bool MqttBridge::publish_homeassistant_discovery(
    const service::MqttBridgeSnapshot& snapshot,
    const bool force_republish) noexcept {
    if (!transport_enabled_.load(std::memory_order_acquire)) {
        return false;
    }
    ensure_gateway_id_hex();

    bool published_any = false;

    // One-time legacy discovery cleanup (plan S4 MQTT #14/#17: publish
    // deletion payloads for old short-address discovery entities before
    // publishing DeviceId-based discovery). Fires at most once per
    // MqttBridge lifetime (reset alongside the rest of the sync state by
    // reset_sync_cache()) -- discovery publishing bypasses the pending-
    // publication queue entirely (it calls hal_mqtt_publish() directly, so
    // it is not observable via drain_publications()), so it needs its own
    // one-time trigger independent of sync_snapshot()'s tombstone sweep.
    if (!legacy_discovery_tombstoned_) {
        for (std::size_t i = 0; i < snapshot.device_count && i < snapshot.devices.size(); ++i) {
            const service::MqttBridgeDeviceSnapshot& current = snapshot.devices[i];
            if (current.short_addr == service::kUnknownShortAddr || !current.online) {
                continue;
            }
            const std::size_t tombstone_count = build_legacy_homeassistant_discovery_tombstones(
                current.short_addr, discovery_messages_scratch_, kMaxDiscoveryMessagesPerDevice);
            for (std::size_t msg_idx = 0; msg_idx < tombstone_count; ++msg_idx) {
#ifdef ESP_PLATFORM
                if (hal_mqtt_publish(
                        discovery_messages_scratch_[msg_idx].topic,
                        discovery_messages_scratch_[msg_idx].payload,
                        true,
                        kMqttQosAtLeastOnce) == HAL_MQTT_STATUS_OK) {
                    published_any = true;
                }
#else
                published_any = true;
#endif
            }
        }
        legacy_discovery_tombstoned_ = true;
    }

    if (!gateway_id_hex_ready_) {
        return published_any;
    }

    for (std::size_t i = 0; i < snapshot.device_count && i < snapshot.devices.size(); ++i) {
        const service::MqttBridgeDeviceSnapshot& current = snapshot.devices[i];
        if (current.short_addr == service::kUnknownShortAddr || !current.online ||
            current.device_id_hex[0] == '\0') {
            continue;
        }

        const service::MqttBridgeDeviceSnapshot* previous = nullptr;
        if (cache_initialized_) {
            previous = find_cached_device_by_device_id_hex(
                cached_devices_, cached_device_count_, current.device_id_hex.data());
        }

        const bool should_publish = force_republish || previous == nullptr ||
                                    (previous != nullptr && discovery_schema_changed(*previous, current));
        if (!should_publish) {
            continue;
        }

        const std::size_t count = build_homeassistant_discovery_messages(
            gateway_id_hex_,
            current,
            discovery_messages_scratch_,
            kMaxDiscoveryMessagesPerDevice);
        for (std::size_t msg_idx = 0; msg_idx < count; ++msg_idx) {
#ifdef ESP_PLATFORM
            if (hal_mqtt_publish(
                    discovery_messages_scratch_[msg_idx].topic,
                    discovery_messages_scratch_[msg_idx].payload,
                    true,
                    kMqttQosAtLeastOnce) ==
                HAL_MQTT_STATUS_OK) {
                published_any = true;
            }
#else
            published_any = true;
#endif
        }
    }

    if (force_republish) {
        discovery_republish_requested_ = false;
    }
    return published_any;
}

uint32_t MqttBridge::next_command_correlation_id() noexcept {
    const uint32_t next = next_correlation_id_.fetch_add(1U, std::memory_order_relaxed);
    if (next != service::kNoCorrelationId) {
        return next;
    }
    return next_correlation_id_.fetch_add(1U, std::memory_order_relaxed);
}

#ifdef ESP_PLATFORM
void MqttBridge::on_transport_connected(void* context) noexcept {
    auto* bridge = static_cast<MqttBridge*>(context);
    if (bridge != nullptr) {
        bridge->handle_transport_connected();
        (void)bridge->subscribe_command_topics();
    }
}

void MqttBridge::on_transport_disconnected(void* context) noexcept {
    auto* bridge = static_cast<MqttBridge*>(context);
    if (bridge != nullptr) {
        bridge->handle_transport_disconnected();
    }
}

void MqttBridge::on_transport_message(
    void* context,
    const char* topic,
    const std::size_t topic_len,
    const uint8_t* payload,
    const std::size_t payload_len) noexcept {
    auto* bridge = static_cast<MqttBridge*>(context);
    if (bridge == nullptr || topic == nullptr || payload == nullptr || topic_len == 0U || payload_len == 0U) {
        return;
    }

    if (topic_len >= kTopicMaxLen || payload_len >= kMqttPayloadMaxLen) {
        return;
    }

    char topic_buf[kTopicMaxLen]{};
    char payload_buf[kMqttPayloadMaxLen]{};
    std::memcpy(topic_buf, topic, topic_len);
    topic_buf[topic_len] = '\0';
    std::memcpy(payload_buf, payload, payload_len);
    payload_buf[payload_len] = '\0';

    (void)bridge->handle_command_message(topic_buf, payload_buf, bridge->next_command_correlation_id());
}

void MqttBridge::task_entry(void* arg) noexcept {
    auto* bridge = static_cast<MqttBridge*>(arg);
    if (bridge != nullptr) {
        bridge->run_loop();
    }
    vTaskDelete(nullptr);
}

void MqttBridge::run_loop() noexcept {
    while (started()) {
        (void)publish_pending_publications();
        (void)sync_runtime_snapshot();
        (void)publish_pending_publications();
        vTaskDelay(kMqttBridgeTaskPeriodTicks);
    }
    task_handle_ = nullptr;
}

bool MqttBridge::ensure_task_started() noexcept {
    if (!started()) {
        return false;
    }
    if (task_handle_ != nullptr) {
        return true;
    }
    if (runtime_ == nullptr) {
        return true;
    }

    TaskHandle_t handle = nullptr;
    const BaseType_t created = xTaskCreate(
        &MqttBridge::task_entry,
        kMqttBridgeTaskName,
        kMqttBridgeTaskStackSize,
        this,
        kMqttBridgeTaskPriority,
        &handle);
    if (created != pdPASS) {
        return false;
    }

    task_handle_ = handle;
    return true;
}

bool MqttBridge::start_transport() noexcept {
    hal_mqtt_callbacks_t callbacks{};
    callbacks.on_connected = &MqttBridge::on_transport_connected;
    callbacks.on_disconnected = &MqttBridge::on_transport_disconnected;
    callbacks.on_message = &MqttBridge::on_transport_message;

    const hal_mqtt_config_t transport_config = build_transport_config();
    const hal_mqtt_status_t init_status = hal_mqtt_init(&transport_config);
    if (init_status == HAL_MQTT_STATUS_DISABLED) {
        ESP_LOGW(kTag, "MQTT transport disabled in Kconfig; bridge will run without broker transport");
        set_runtime_status(false, false, MqttConnectionError::kDisabled);
        publish_runtime_status();
        return false;
    }
    if (init_status != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT transport init failed status=%d", static_cast<int>(init_status));
        handle_transport_error(MqttConnectionError::kInitFailed);
        return false;
    }

    char broker_summary[service::NetworkApiSnapshot::MqttStatusSnapshot::kBrokerEndpointSummaryMaxLen]{};
    if (hal_mqtt_get_broker_endpoint_summary(broker_summary, sizeof(broker_summary)) == HAL_MQTT_STATUS_OK) {
        std::memcpy(
            runtime_status_cache_.broker_endpoint_summary.data(),
            broker_summary,
            sizeof(broker_summary));
    } else {
        runtime_status_cache_.broker_endpoint_summary[0] = '\0';
    }

    if (hal_mqtt_register_callbacks(&callbacks, this) != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT transport callback registration failed");
        handle_transport_error(MqttConnectionError::kInitFailed);
        return false;
    }

    const hal_mqtt_status_t start_status = hal_mqtt_start();
    if (start_status != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT transport start failed status=%d", static_cast<int>(start_status));
        handle_transport_error(MqttConnectionError::kStartFailed);
        return false;
    }

    set_runtime_status(true, false, MqttConnectionError::kNone);
    publish_runtime_status();
    return true;
}

bool MqttBridge::subscribe_command_topics() noexcept {
    if (command_topics_subscribed_.load(std::memory_order_acquire)) {
        return true;
    }

    // Plan S4 MQTT #13: legacy short-address command wildcards are not
    // subscribed in production. Only the v1 DeviceId-keyed wildcards are
    // subscribed here; handle_command_message()'s legacy branch remains in
    // the binary for host tests/dev-profile use but a real broker never
    // delivers a legacy-shaped topic to it once this is the only
    // subscription made.
    if (hal_mqtt_subscribe(topic_v1_device_config_set_wildcard(), kMqttQosAtLeastOnce) != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT v1 config topic subscription failed");
        handle_transport_subscribe_failure();
        return false;
    }
    if (hal_mqtt_subscribe(topic_v1_device_power_set_wildcard(), kMqttQosAtLeastOnce) != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT v1 power topic subscription failed");
        handle_transport_subscribe_failure();
        return false;
    }

    command_topics_subscribed_.store(true, std::memory_order_release);
    if (hal_mqtt_is_connected()) {
        set_runtime_status(true, true, MqttConnectionError::kNone);
        publish_runtime_status();
    }
    return true;
}
#endif

}  // namespace mqtt_bridge
