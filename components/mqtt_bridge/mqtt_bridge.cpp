/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_bridge.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core_commands.hpp"
#include "log_tags.h"
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
constexpr const char* kMqttBridgeTaskName = "mqtt_bridge";
constexpr uint32_t kMqttBridgeTaskStackSize = 4608U;
constexpr UBaseType_t kMqttBridgeTaskPriority = 4U;
constexpr TickType_t kMqttBridgeTaskPeriodTicks = pdMS_TO_TICKS(1000);
#endif
constexpr std::size_t kDrainBatchCapacity = 8U;

bool parse_u32_strict(const char* text, uint32_t* out) noexcept {
    if (text == nullptr || out == nullptr || text[0] == '\0') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX) {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool extract_device_short_addr_from_topic(const char* topic, uint16_t* out_short_addr) noexcept {
    if (topic == nullptr || out_short_addr == nullptr) {
        return false;
    }

    constexpr const char* kPrefix = "zigbee-gateway/devices/";
    constexpr const char* kSuffix = "/config";
    const std::size_t prefix_len = std::strlen(kPrefix);
    const std::size_t suffix_len = std::strlen(kSuffix);
    const std::size_t topic_len = std::strlen(topic);
    if (topic_len <= prefix_len + suffix_len) {
        return false;
    }
    if (std::strncmp(topic, kPrefix, prefix_len) != 0) {
        return false;
    }
    if (std::strcmp(topic + topic_len - suffix_len, kSuffix) != 0) {
        return false;
    }

    const std::size_t short_len = topic_len - prefix_len - suffix_len;
    if (short_len == 0U || short_len >= 8U) {
        return false;
    }
    char short_buf[8] = {0};
    std::memcpy(short_buf, topic + prefix_len, short_len);
    short_buf[short_len] = '\0';

    uint32_t short_raw = 0;
    if (!parse_u32_strict(short_buf, &short_raw) || short_raw == 0U || short_raw > 0xFFFFU) {
        return false;
    }

    *out_short_addr = static_cast<uint16_t>(short_raw);
    return true;
}

bool find_json_u32_field(const char* body, const char* key, uint32_t* out_value) noexcept {
    if (body == nullptr || key == nullptr || out_value == nullptr) {
        return false;
    }

    char pattern[64]{};
    const int pattern_written = std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pattern_written <= 0 || static_cast<std::size_t>(pattern_written) >= sizeof(pattern)) {
        return false;
    }

    const char* key_pos = std::strstr(body, pattern);
    if (key_pos == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key_pos + pattern_written, ':');
    if (colon == nullptr) {
        return false;
    }

    const char* value = colon + 1;
    while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') {
        ++value;
    }

    char number_buf[16]{};
    std::size_t idx = 0;
    while (value[idx] >= '0' && value[idx] <= '9' && idx + 1U < sizeof(number_buf)) {
        number_buf[idx] = value[idx];
        ++idx;
    }
    number_buf[idx] = '\0';

    if (idx == 0U) {
        return false;
    }

    return parse_u32_strict(number_buf, out_value);
}

bool parse_reporting_config_payload(
    const char* payload,
    core::CoreCommand* out_command) noexcept {
    if (payload == nullptr || out_command == nullptr) {
        return false;
    }

    uint32_t endpoint = 0;
    uint32_t cluster_id = 0;
    uint32_t min_interval = 0;
    uint32_t max_interval = 0;
    if (!find_json_u32_field(payload, "endpoint", &endpoint) ||
        !find_json_u32_field(payload, "cluster_id", &cluster_id) ||
        !find_json_u32_field(payload, "min_interval_seconds", &min_interval) ||
        !find_json_u32_field(payload, "max_interval_seconds", &max_interval)) {
        return false;
    }

    if (endpoint == 0U || endpoint > 0xFFU ||
        cluster_id == 0U || cluster_id > 0xFFFFU ||
        min_interval > 0xFFFFU || max_interval > 0xFFFFU) {
        return false;
    }
    if (max_interval == 0U || min_interval > max_interval) {
        return false;
    }

    uint32_t reportable_change = 0;
    (void)find_json_u32_field(payload, "reportable_change", &reportable_change);

    uint32_t capability_flags = 0;
    if (find_json_u32_field(payload, "capability_flags", &capability_flags)) {
        if (capability_flags > 0xFFU) {
            return false;
        }
        out_command->reporting_capability_flags = static_cast<uint8_t>(capability_flags);
    }

    out_command->reporting_endpoint = static_cast<uint8_t>(endpoint);
    out_command->reporting_cluster_id = static_cast<uint16_t>(cluster_id);
    out_command->reporting_min_interval_seconds = static_cast<uint16_t>(min_interval);
    out_command->reporting_max_interval_seconds = static_cast<uint16_t>(max_interval);
    out_command->reporting_reportable_change = reportable_change;
    return true;
}

}  // namespace

bool MqttBridge::start() noexcept {
    publish_discovery();
    reset_sync_cache();
    started_.store(true, std::memory_order_release);
#ifdef ESP_PLATFORM
    return ensure_task_started();
#else
    return started();
#endif
}

void MqttBridge::stop() noexcept {
    reset_sync_cache();
    started_.store(false, std::memory_order_release);
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
#ifdef ESP_PLATFORM
    (void)ensure_task_started();
#endif
}

bool MqttBridge::handle_config_command(const char* topic, const char* payload, uint32_t correlation_id) noexcept {
    if (runtime_ == nullptr || correlation_id == core::kNoCorrelationId) {
        return false;
    }

    uint16_t short_addr = core::kUnknownDeviceShortAddr;
    if (!extract_device_short_addr_from_topic(topic, &short_addr)) {
        return false;
    }

    core::CoreCommand command{};
    command.type = core::CoreCommandType::kUpdateReportingProfile;
    command.correlation_id = correlation_id;
    command.device_short_addr = short_addr;

    if (!parse_reporting_config_payload(payload, &command)) {
        return false;
    }

    return runtime_->post_command(command) == core::CoreError::kOk;
}

std::size_t MqttBridge::sync_runtime_snapshot() noexcept {
    if (!started() || runtime_ == nullptr) {
        return 0U;
    }

    return sync_snapshot(runtime_->state());
}

std::size_t MqttBridge::publish_pending_publications() noexcept {
    MqttPublishedMessage batch[kDrainBatchCapacity]{};
    std::size_t published = 0U;

    while (true) {
        const std::size_t drained = drain_publications(batch, kDrainBatchCapacity);
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

void MqttBridge::reset_sync_cache() noexcept {
    cached_device_count_ = 0;
    cache_initialized_ = false;
    pending_publication_count_ = 0;
}

bool MqttBridge::publish_message(const MqttPublishedMessage& message) noexcept {
#ifdef ESP_PLATFORM
    ESP_LOGI(kTag, "publish topic=%s retain=%s payload=%s", message.topic, message.retain ? "true" : "false", message.payload);
#else
    (void)message;
#endif
    return true;
}

#ifdef ESP_PLATFORM
void MqttBridge::task_entry(void* arg) noexcept {
    auto* bridge = static_cast<MqttBridge*>(arg);
    if (bridge != nullptr) {
        bridge->run_loop();
    }
    vTaskDelete(nullptr);
}

void MqttBridge::run_loop() noexcept {
    while (started()) {
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
#endif

}  // namespace mqtt_bridge
