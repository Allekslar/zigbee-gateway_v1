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

const service::MqttBridgeDeviceSnapshot* find_cached_device_by_short(
    const service::MqttBridgeDeviceSnapshot* devices,
    const uint16_t count,
    const uint16_t short_addr) noexcept {
    if (devices == nullptr) {
        return nullptr;
    }
    for (uint16_t i = 0; i < count; ++i) {
        if (devices[i].short_addr == short_addr) {
            return &devices[i];
        }
    }
    return nullptr;
}

}  // namespace

bool MqttBridge::start() noexcept {
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
}

bool MqttBridge::started() const noexcept {
    return started_.load(std::memory_order_acquire);
}

void MqttBridge::attach_runtime(service::ServiceRuntimeApi* runtime) noexcept {
    runtime_ = runtime;
    publish_runtime_status();
#ifdef ESP_PLATFORM
    (void)ensure_task_started();
#endif
}

bool MqttBridge::handle_command_message(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (topic == nullptr || payload == nullptr) {
        return false;
    }

    if (service::mqtt_topic_has_suffix(topic, "/config")) {
        return handle_config_command(topic, payload, correlation_id);
    }
    if (service::mqtt_topic_has_suffix(topic, "/power/set")) {
        return handle_power_command(topic, payload, correlation_id);
    }

    return false;
}

bool MqttBridge::handle_config_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (runtime_ == nullptr || correlation_id == service::kNoCorrelationId) {
        return false;
    }

    service::ConfigManager::ReportingProfile profile{};
    if (service::parse_mqtt_reporting_profile_request(topic, payload, &profile) !=
        service::ApplicationCommandParseStatus::kOk) {
        return false;
    }

    return runtime_->post_reporting_profile_write(profile);
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
        MqttPublishedMessage publication{};
        if (!topic_device_state(device.short_addr, publication.topic, sizeof(publication.topic))) {
            return;
        }
        const int written = std::snprintf(
            publication.payload,
            sizeof(publication.payload),
            "{\"power_on\":%s}",
            device.power_on ? "true" : "false");
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(publication.payload)) {
            return;
        }
        publication.retain = true;
        if (pending_publication_count_ >= kMaxMqttPublicationsPerSync) {
            return;
        }
        pending_publications_[pending_publication_count_++] = publication;
        return;
    }
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

    bool published_any = false;
    for (std::size_t i = 0; i < snapshot.device_count && i < snapshot.devices.size(); ++i) {
        const service::MqttBridgeDeviceSnapshot& current = snapshot.devices[i];
        if (current.short_addr == service::kUnknownShortAddr || !current.online) {
            continue;
        }

        const service::MqttBridgeDeviceSnapshot* previous = nullptr;
        if (cache_initialized_) {
            previous = find_cached_device_by_short(cached_devices_, cached_device_count_, current.short_addr);
        }

        const bool should_publish = force_republish || previous == nullptr ||
                                    (previous != nullptr && discovery_schema_changed(*previous, current));
        if (!should_publish) {
            continue;
        }

        const std::size_t count = build_homeassistant_discovery_messages(
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

    if (hal_mqtt_subscribe(topic_device_config_wildcard(), kMqttQosAtLeastOnce) != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT config topic subscription failed");
        handle_transport_subscribe_failure();
        return false;
    }
    if (hal_mqtt_subscribe(topic_device_power_set_wildcard(), kMqttQosAtLeastOnce) != HAL_MQTT_STATUS_OK) {
        ESP_LOGW(kTag, "MQTT power topic subscription failed");
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
