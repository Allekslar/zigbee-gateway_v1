/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "esp_log.h"
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

core::CoreRegistry g_registry;
service::EffectExecutor g_effect_executor;
service::ServiceRuntime g_runtime(g_registry, g_effect_executor);
web_ui::WebServer g_web_server(g_runtime);
mqtt_bridge::MqttBridge g_mqtt;
matter_bridge::MatterBridge g_matter;

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
        ESP_LOGE(kTag, "Service runtime task start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (!g_web_server.start()) {
        ESP_LOGE(kTag, "Web server start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    g_mqtt.attach_runtime(&g_runtime);
    if (!g_mqtt.start()) {
        ESP_LOGE(kTag, "MQTT bridge start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    g_matter.attach_runtime(&g_runtime);
    if (!g_matter.start()) {
        ESP_LOGE(kTag, "Matter bridge start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
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
