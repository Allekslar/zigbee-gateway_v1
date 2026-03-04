/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_event_adapter_internal.hpp"

#include "hal_nvs.h"
#include "hal_wifi.h"
#include "hal_zigbee.h"
#include "log_tags.h"
#include "service_runtime.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SERVICE_HAL_ADAPTER;
#define HAL_ADAPTER_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define HAL_ADAPTER_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#define HAL_ADAPTER_LOGE(...) ESP_LOGE(kTag, __VA_ARGS__)
#else
#define HAL_ADAPTER_LOGI(...) ((void)0)
#define HAL_ADAPTER_LOGW(...) ((void)0)
#define HAL_ADAPTER_LOGE(...) ((void)0)
#endif

core::CoreCommandResultType map_zigbee_result(hal_zigbee_result_t result) noexcept {
    switch (result) {
        case HAL_ZIGBEE_RESULT_SUCCESS:
            return core::CoreCommandResultType::kSuccess;
        case HAL_ZIGBEE_RESULT_TIMEOUT:
            return core::CoreCommandResultType::kTimeout;
        case HAL_ZIGBEE_RESULT_FAILED:
        default:
            return core::CoreCommandResultType::kFailed;
    }
}

void zigbee_device_joined_cb(void* context, uint16_t short_addr) noexcept {
    if (context == nullptr) {
        return;
    }

    if (short_addr == core::kUnknownDeviceShortAddr) {
        HAL_ADAPTER_LOGW("Ignore kDeviceJoined with unknown short_addr=0x%04x", (unsigned)short_addr);
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    const bool queued = runtime->post_zigbee_join_candidate(short_addr);
    if (!queued) {
        HAL_ADAPTER_LOGW("Drop Zigbee join candidate short_addr=0x%04x", (unsigned)short_addr);
    }
    if (queued) {
        HAL_ADAPTER_LOGI("Accepted Zigbee join candidate short_addr=0x%04x", (unsigned)short_addr);
    }
}

void zigbee_device_left_cb(void* context, uint16_t short_addr) noexcept {
    if (context == nullptr) {
        return;
    }

    if (short_addr == core::kUnknownDeviceShortAddr) {
        HAL_ADAPTER_LOGW("Ignore kDeviceLeft with unknown short_addr=0x%04x", (unsigned)short_addr);
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    core::CoreEvent event{};
    event.type = core::CoreEventType::kDeviceLeft;
    event.device_short_addr = short_addr;
    const bool posted = runtime->post_event(event);
    if (!posted) {
        HAL_ADAPTER_LOGW("Drop kDeviceLeft event for short_addr=0x%04x", (unsigned)short_addr);
    }
    if (posted) {
        HAL_ADAPTER_LOGI("Posted kDeviceLeft event for short_addr=0x%04x", (unsigned)short_addr);
    }
}

void zigbee_attribute_report_cb(
    void* context,
    uint16_t short_addr,
    uint16_t cluster_id,
    uint16_t attribute_id,
    bool value_bool,
    uint32_t value_u32) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    core::CoreEvent event{};
    event.type = core::CoreEventType::kAttributeReported;
    event.device_short_addr = short_addr;
    event.cluster_id = cluster_id;
    event.attribute_id = attribute_id;
    event.value_bool = value_bool;
    event.value_u32 = value_u32;
    if (!runtime->post_event(event)) {
        HAL_ADAPTER_LOGW(
            "Drop kAttributeReported short_addr=0x%04x cluster=0x%04x attr=0x%04x",
            (unsigned)short_addr,
            (unsigned)cluster_id,
            (unsigned)attribute_id);
    }
}

void zigbee_command_result_cb(void* context, uint32_t correlation_id, hal_zigbee_result_t result) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    core::CoreCommandResult command_result{};
    command_result.correlation_id = correlation_id;
    command_result.completed_at_ms = 0;
    command_result.type = map_zigbee_result(result);
    if (runtime->handle_command_result(command_result) != core::CoreError::kOk) {
        HAL_ADAPTER_LOGW(
            "Drop command result correlation_id=%lu result=%u",
            static_cast<unsigned long>(correlation_id),
            static_cast<unsigned>(result));
    }
}

void zigbee_interview_result_cb(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    if (!runtime->post_zigbee_interview_result(correlation_id, short_addr, result)) {
        HAL_ADAPTER_LOGW(
            "Drop interview result short_addr=0x%04x correlation_id=%lu result=%u",
            static_cast<unsigned>(short_addr),
            static_cast<unsigned long>(correlation_id),
            static_cast<unsigned>(result));
    }
}

void zigbee_bind_result_cb(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    if (!runtime->post_zigbee_bind_result(correlation_id, short_addr, result)) {
        HAL_ADAPTER_LOGW(
            "Drop bind result short_addr=0x%04x correlation_id=%lu result=%u",
            static_cast<unsigned>(short_addr),
            static_cast<unsigned long>(correlation_id),
            static_cast<unsigned>(result));
    }
}

void zigbee_configure_reporting_result_cb(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    if (!runtime->post_zigbee_configure_reporting_result(correlation_id, short_addr, result)) {
        HAL_ADAPTER_LOGW(
            "Drop configure-reporting result short_addr=0x%04x correlation_id=%lu result=%u",
            static_cast<unsigned>(short_addr),
            static_cast<unsigned long>(correlation_id),
            static_cast<unsigned>(result));
    }
}

void zigbee_attribute_report_raw_cb(void* context, const hal_zigbee_raw_attribute_report_t* report) noexcept {
    if (context == nullptr || report == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    if (!runtime->post_zigbee_attribute_report_raw(*report)) {
        HAL_ADAPTER_LOGW(
            "Drop raw attribute report short_addr=0x%04x cluster=0x%04x attr=0x%04x type=0x%02x len=%u",
            static_cast<unsigned>(report->short_addr),
            static_cast<unsigned>(report->cluster_id),
            static_cast<unsigned>(report->attribute_id),
            static_cast<unsigned>(report->zcl_data_type),
            static_cast<unsigned>(report->payload_len));
    }
}

void wifi_network_up_cb(void* context) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    core::CoreEvent event{};
    event.type = core::CoreEventType::kNetworkUp;
    if (!runtime->post_event(event)) {
        HAL_ADAPTER_LOGW("Drop kNetworkUp event");
    }
}

void wifi_network_down_cb(void* context) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    core::CoreEvent event{};
    event.type = core::CoreEventType::kNetworkDown;
    if (!runtime->post_event(event)) {
        HAL_ADAPTER_LOGW("Drop kNetworkDown event");
    }
}

void nvs_u32_written_cb(void* context, const char* key, uint32_t value) noexcept {
    if (context == nullptr) {
        return;
    }

    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(context);
    runtime->on_nvs_u32_written(key, value);
}

}  // namespace

bool init_hal_event_adapter(ServiceRuntime& runtime) noexcept {
    ServiceRuntime* runtime_ptr = &runtime;

    if (hal_nvs_init() != HAL_NVS_STATUS_OK || hal_wifi_init() != HAL_WIFI_STATUS_OK) {
        return false;
    }

    hal_zigbee_callbacks_t zigbee_callbacks{};
    zigbee_callbacks.on_device_joined = &zigbee_device_joined_cb;
    zigbee_callbacks.on_device_left = &zigbee_device_left_cb;
    zigbee_callbacks.on_attribute_report = &zigbee_attribute_report_cb;
    zigbee_callbacks.on_attribute_report_raw = &zigbee_attribute_report_raw_cb;
    zigbee_callbacks.on_command_result = &zigbee_command_result_cb;
    zigbee_callbacks.on_interview_result = &zigbee_interview_result_cb;
    zigbee_callbacks.on_bind_result = &zigbee_bind_result_cb;
    zigbee_callbacks.on_configure_reporting_result = &zigbee_configure_reporting_result_cb;

    hal_wifi_callbacks_t wifi_callbacks{};
    wifi_callbacks.on_network_up = &wifi_network_up_cb;
    wifi_callbacks.on_network_down = &wifi_network_down_cb;

    hal_nvs_callbacks_t nvs_callbacks{};
    nvs_callbacks.on_u32_written = &nvs_u32_written_cb;

    const hal_zigbee_status_t zigbee_err = hal_zigbee_register_callbacks(&zigbee_callbacks, runtime_ptr);
    const hal_wifi_status_t wifi_err = hal_wifi_register_callbacks(&wifi_callbacks, runtime_ptr);
    const hal_nvs_status_t nvs_err = hal_nvs_register_callbacks(&nvs_callbacks, runtime_ptr);
    if (zigbee_err != HAL_ZIGBEE_STATUS_OK || wifi_err != HAL_WIFI_STATUS_OK || nvs_err != HAL_NVS_STATUS_OK) {
        HAL_ADAPTER_LOGE(
            "HAL callback registration failed zigbee=%d wifi=%d nvs=%d",
            (int)zigbee_err,
            wifi_err,
            nvs_err);
        return false;
    }

    if (runtime_ptr->has_saved_wifi_credentials()) {
        runtime_ptr->mark_wifi_credentials_available();
        if (runtime_ptr->ensure_zigbee_started()) {
            HAL_ADAPTER_LOGI("Saved Wi-Fi credentials found, Zigbee started");
        } else {
            HAL_ADAPTER_LOGW("Saved Wi-Fi credentials found, Zigbee start attempt failed");
        }
    } else {
        HAL_ADAPTER_LOGI("Zigbee startup deferred: no Wi-Fi credentials");
    }

    return true;
}

}  // namespace service
