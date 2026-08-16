/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_registry.hpp"
#include <inttypes.h>
#include "effect_executor.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_id.hpp"
#include "hal_matter.h"
#include "hal_mdns.h"
#include "log_tags.h"
#include "matter_bridge.hpp"
#include "mqtt_bridge.hpp"
#include "ota_bootstrap.hpp"
#include "provisioning_secrets.hpp"
#include "sdkconfig.h"
#include "security_bounds.hpp"
#include "service_runtime.hpp"
#include "web_server.hpp"

namespace {

constexpr const char* kTag = LOG_TAG_APP_MAIN;
constexpr TickType_t kDeferredZigbeeStartDelayTicks = pdMS_TO_TICKS(15000);
constexpr const char* kDeferredZigbeeTaskName = "zigbee_start";
constexpr uint32_t kDeferredZigbeeTaskStackSize = 4096U;
constexpr UBaseType_t kDeferredZigbeeTaskPriority = 4U;

core::CoreRegistry g_registry;
service::EffectExecutor g_effect_executor;
service::ServiceRuntime g_runtime(g_registry, g_effect_executor);
web_ui::WebServer g_web_server(g_runtime);
mqtt_bridge::MqttBridge g_mqtt;
matter_bridge::MatterBridge g_matter;

void deferred_zigbee_start_task(void* arg) {
    auto* runtime = static_cast<service::ServiceRuntime*>(arg);
    if (runtime == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    vTaskDelay(kDeferredZigbeeStartDelayTicks);
    const bool started = runtime->ensure_zigbee_started();
    ESP_LOGI(kTag, "Deferred Zigbee start after bootstrap window, started=%s", started ? "yes" : "no");
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main(void) {
    // Capabilities are computed once from Kconfig/HAL truth sources and handed
    // to the service-owned RuntimeCapabilities projection before anything else
    // starts. Adapters/UI must consult g_runtime.capabilities() rather than
    // reading Kconfig or probing HAL weak symbols themselves.
    service::RuntimeCapabilities caps{};
#if CONFIG_ZGW_ZIGBEE_ENABLED
    caps.zigbee_available = true;
#endif
#if CONFIG_ZGW_MQTT_TRANSPORT_ENABLED
    caps.mqtt_available = true;
#endif
    caps.matter_target_available = hal_matter_available();
#if CONFIG_ZGW_OTA_ENABLED
    caps.ota_available = true;
#endif
#if CONFIG_ZGW_RCP_TARGET_BACKEND_UART
    caps.rcp_update_available = true;
#endif
    g_runtime.set_capabilities(caps);

    if (!g_runtime.initialize_hal_adapter()) {
        ESP_LOGE(kTag, "HAL adapter init failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Plan-independent, real-hardware-found ordering fix: g_runtime.
    // start() (creates the runtime/scan-worker/RCP-worker tasks) has no
    // real dependency on Wi-Fi connectivity -- config_manager_.load()
    // already ran in ServiceRuntime's own constructor, well before
    // app_main() starts. Deliberately called HERE, before Wi-Fi
    // autoconnect/AP start below, rather than after (its own original
    // position): a real "Stack protection fault" was found on real
    // ESP32-C6 hardware where this call's own task-stack allocation
    // (hal_alloc_internal_sram(), MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT|
    // MALLOC_CAP_DMA) failed -- confirmed via real heap diagnostics that
    // a successful Wi-Fi STATION connection (this project's very first
    // one ever exercised on real hardware, requested via a real user
    // Wi-Fi network) consumes roughly 75KB of internal SRAM (station
    // buffers, DHCP/ARP, lwIP) by this point in boot, leaving only ~2KB
    // DMA-capable free where 9216 bytes are needed. Calling this before
    // Wi-Fi starts lets these tasks claim their stacks while internal
    // SRAM is still almost entirely free (~77KB observed).
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

    const service::ServiceRuntime::BootAutoconnectResult autoconnect_result =
        g_runtime.autoconnect_from_saved_credentials();

    const bool should_start_provisioning_ap =
        autoconnect_result == service::ServiceRuntime::BootAutoconnectResult::kCredentialsMissing ||
        autoconnect_result == service::ServiceRuntime::BootAutoconnectResult::kConnectFailed;
    if (should_start_provisioning_ap) {
        // Plan S6 "Provisioning and credentials" #1 + #4: no shared/
        // default production secret -- SSID includes a non-secret
        // gateway suffix, passphrase is 16 cryptographically random
        // Base32 characters generated fresh every boot (never persisted,
        // matching "every shared/default production secret" being
        // removed rather than replaced with a different fixed one).
        char provisioning_ssid[32]{};
        char provisioning_passphrase[service::SecurityBounds::kProvisioningPassphraseBase32Chars + 1U]{};
        const bool ssid_built = service::build_provisioning_ap_ssid(
            g_runtime.gateway_id(), service::kGatewayHostNamePrefix, provisioning_ssid, sizeof(provisioning_ssid));
        const bool passphrase_generated =
            service::generate_provisioning_passphrase(provisioning_passphrase, sizeof(provisioning_passphrase));
        if (!ssid_built || !passphrase_generated) {
            ESP_LOGE(kTag, "Provisioning AP credential generation failed (ssid=%d, passphrase=%d)",
                     (int)ssid_built, (int)passphrase_generated);
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        if (!g_runtime.start_provisioning_ap(provisioning_ssid, provisioning_passphrase)) {
            ESP_LOGE(kTag, "Wi-Fi AP start failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        // Development-only delivery channel: this device has no display,
        // only UART. Printing the freshly-generated passphrase to the
        // serial console is the only way an installer can currently
        // learn it -- the same interim posture plan #2 explicitly
        // sanctions for its own analogous case ("development adapter may
        // generate and print a one-time secret"). A real production
        // channel (printed label, QR code, or similar manufacturing
        // step) is not designed yet -- see docs/security/
        // CONTROL_PLANE_SECURITY.md Section 3 for this named follow-up.
        ESP_LOGI(kTag, "Wi-Fi AP started (SSID=%s, passphrase=%s) -- development-only console delivery, "
                       "see docs/security/CONTROL_PLANE_SECURITY.md",
                 provisioning_ssid, provisioning_passphrase);
    } else {
        ESP_LOGI(kTag, "Wi-Fi AP skipped: saved credentials available and autoconnect started");
    }

    // Plan S6 "HTTPS and sessions" #9: "Derive production mDNS host
    // exactly as zigbee-gateway-<last6>.local and advertise only
    // https://." build_gateway_mdns_host() itself picks the
    // production-suffixed vs. plain-development hostname
    // (CONFIG_ZGW_PRODUCTION_PROFILE); the advertised URL scheme below
    // mirrors web_server.cpp's own plan #7 choice of listener for this
    // exact build (production speaks HTTPS only, development speaks
    // plain HTTP) -- never claims https:// for a listener that is
    // actually serving plain HTTP, which would actively mislead an
    // installer into a URL that cannot work.
    char mdns_host[32]{};
    if (!service::build_gateway_mdns_host(g_runtime.gateway_id(), mdns_host, sizeof(mdns_host))) {
        ESP_LOGE(kTag, "mDNS host name generation failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    if (hal_mdns_start(mdns_host) != 0) {
        ESP_LOGE(kTag, "mDNS start failed for host '%s'", mdns_host);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
#if defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
    ESP_LOGI(kTag, "mDNS started: https://%s.local", mdns_host);
#else
    ESP_LOGI(kTag, "mDNS started: http://%s.local", mdns_host);
#endif

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

    if (!g_web_server.start()) {
        ESP_LOGE(kTag, "Web server start failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (!caps.mqtt_available) {
        ESP_LOGI(kTag, "MQTT bridge not started: capability unavailable (CONFIG_ZGW_MQTT_TRANSPORT_ENABLED=n)");
    } else {
        g_mqtt.attach_runtime(&g_runtime);
        if (!g_mqtt.start()) {
            ESP_LOGE(kTag, "MQTT bridge start failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

    if (!caps.matter_target_available) {
        ESP_LOGI(kTag, "Matter bridge not started: capability unavailable (target adapter not linked)");
    } else {
        g_matter.attach_runtime(&g_runtime);
        if (!g_matter.start()) {
            ESP_LOGE(kTag, "Matter bridge start failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

    if (!caps.zigbee_available) {
        ESP_LOGW(kTag, "Zigbee not started: capability unavailable (CONFIG_ZGW_ZIGBEE_ENABLED=n)");
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
