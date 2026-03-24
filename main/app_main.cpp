/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_registry.hpp"
#include <inttypes.h>
#include "effect_executor.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_mdns.h"
#include "log_tags.h"
#include "matter_bridge.hpp"
#include "mqtt_bridge.hpp"
#include "ota_bootstrap.hpp"
#include "sdkconfig.h"
#include "service_runtime.hpp"
#include "web_server.hpp"

namespace {

constexpr const char* kTag = LOG_TAG_APP_MAIN;
constexpr const char* kGatewayHostName = "zigbee-gateway";
constexpr const char* kProvisioningApPassword = "12345678";
constexpr bool kTemporarilyDisableMqtt = false;
constexpr bool kTemporarilyDisableZigbeeAutoStart = true;
constexpr uint32_t kDeferredZigbeePageLoadIdleMs = 5000U;
constexpr uint32_t kDeferredZigbeeFallbackDelayMs = 30000U;
constexpr uint32_t kDeferredZigbeePollMs = 500U;
constexpr const char* kDeferredZigbeeTaskName = "zigbee_defer";
constexpr uint32_t kDeferredZigbeeTaskStackSize = 4096U;
constexpr UBaseType_t kDeferredZigbeeTaskPriority = 4U;

core::CoreRegistry g_registry;
service::EffectExecutor g_effect_executor;
service::ServiceRuntime g_runtime(g_registry, g_effect_executor);
web_ui::WebServer g_web_server(g_runtime);
mqtt_bridge::MqttBridge g_mqtt;
matter_bridge::MatterBridge g_matter;

uint32_t app_monotonic_now_ms() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void deferred_zigbee_start_task(void* arg) {
    auto* runtime = static_cast<service::ServiceRuntime*>(arg);
    if (runtime == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    const TickType_t poll_ticks = pdMS_TO_TICKS(kDeferredZigbeePollMs);
    while (true) {
        const uint32_t now_ms = app_monotonic_now_ms();
        const bool page_load_ready =
            web_ui::has_page_load_activity() && web_ui::page_load_idle_ms() >= kDeferredZigbeePageLoadIdleMs;
        const bool fallback_ready = now_ms >= kDeferredZigbeeFallbackDelayMs;
        if (page_load_ready || fallback_ready) {
            runtime->request_zigbee_start();
            const bool started = runtime->ensure_zigbee_started();
            ESP_LOGI(
                kTag,
                "Deferred Zigbee start gate satisfied (%s, page_load_seen=%s idle_ms=%" PRIu32 "), started=%s",
                page_load_ready ? "page-load-idle" : "fallback",
                web_ui::has_page_load_activity() ? "yes" : "no",
                web_ui::page_load_idle_ms(),
                started ? "yes" : "no");
            break;
        }
        vTaskDelay(poll_ticks);
    }

    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main(void) {
    if (!g_runtime.initialize_hal_adapter()) {
        ESP_LOGE(kTag, "HAL adapter init failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (!g_runtime.start_provisioning_ap(kGatewayHostName, kProvisioningApPassword)) {
        ESP_LOGE(kTag, "Wi-Fi AP start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(kTag, "Wi-Fi AP started (SSID/host=%s)", kGatewayHostName);

    if (hal_mdns_start(kGatewayHostName) != 0) {
        ESP_LOGE(kTag, "mDNS start failed for host '%s'", kGatewayHostName);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(kTag, "mDNS started: http://%s.local", kGatewayHostName);

    const service::ServiceRuntime::BootAutoconnectResult autoconnect_result =
        g_runtime.autoconnect_from_saved_credentials();
    switch (autoconnect_result) {
        case service::ServiceRuntime::BootAutoconnectResult::kCredentialsMissing:
            ESP_LOGI(kTag, "Saved Wi-Fi credentials not found, AP-only mode");
            break;
        case service::ServiceRuntime::BootAutoconnectResult::kConnectStarted:
            ESP_LOGI(kTag, "Auto-connect started from saved credentials");
            break;
        case service::ServiceRuntime::BootAutoconnectResult::kConnectFailed:
        default:
            ESP_LOGW(kTag, "Auto-connect failed from saved credentials");
            break;
    }

    if (!g_runtime.start()) {
        const service::ConfigManager::LoadReport& config_report = g_runtime.config_load_report();
        ESP_LOGE(
            kTag,
            "Service runtime start failed (config bootstrap status=%u from_schema=%" PRIu32 " to_schema=%" PRIu32 ")",
            static_cast<unsigned>(config_report.status),
            config_report.from_schema_version,
            config_report.to_schema_version);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    const service::ConfigManager::LoadReport& config_report = g_runtime.config_load_report();
    if (config_report.status == service::ConfigManager::LoadStatus::kMigrated) {
        ESP_LOGI(
            kTag,
            "Config schema migration applied (%" PRIu32 " -> %" PRIu32 ")",
            config_report.from_schema_version,
            config_report.to_schema_version);
    } else if (config_report.status == service::ConfigManager::LoadStatus::kFreshInstall) {
        ESP_LOGI(kTag, "Config schema initialized for fresh install (%" PRIu32 ")", config_report.to_schema_version);
    }
    if (config_report.schema_repair_persist_failed) {
        ESP_LOGW(
            kTag,
            "Config schema repair could not be persisted (from_schema=%" PRIu32 " to_schema=%" PRIu32 ")",
            config_report.from_schema_version,
            config_report.to_schema_version);
    }

    if (!g_web_server.start()) {
        ESP_LOGE(kTag, "Web server start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (kTemporarilyDisableMqtt) {
        ESP_LOGW(kTag, "MQTT bridge temporarily disabled for web transport isolation");
    } else {
        g_mqtt.attach_runtime(&g_runtime);
        if (!g_mqtt.start()) {
            ESP_LOGE(kTag, "MQTT bridge start failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }
    g_matter.attach_runtime(&g_runtime);
    if (!g_matter.start()) {
        ESP_LOGE(kTag, "Matter bridge start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (kTemporarilyDisableZigbeeAutoStart) {
        ESP_LOGW(kTag, "Zigbee auto-start temporarily disabled for web transport isolation");
    } else {
        TaskHandle_t deferred_zigbee_task = nullptr;
        if (xTaskCreate(
                &deferred_zigbee_start_task,
                kDeferredZigbeeTaskName,
                kDeferredZigbeeTaskStackSize,
                &g_runtime,
                kDeferredZigbeeTaskPriority,
                &deferred_zigbee_task) != pdPASS) {
            ESP_LOGE(kTag, "Deferred Zigbee start task creation failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

#if CONFIG_ZGW_OTA_ENABLED && CONFIG_ZGW_OTA_BOOT_CONFIRM_ENABLED
    switch (service::confirm_pending_ota_image()) {
        case service::OtaBootConfirmResult::kNotRequired:
            break;
        case service::OtaBootConfirmResult::kConfirmed:
            ESP_LOGI(kTag, "OTA image confirmation completed");
            break;
        case service::OtaBootConfirmResult::kFailed:
        default:
            ESP_LOGE(kTag, "OTA boot confirmation failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;
    }
#endif
}
