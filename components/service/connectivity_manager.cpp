/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "connectivity_manager.hpp"

#include "hal_nvs.h"
#include "hal_wifi.h"
#include "hal_zigbee.h"
#include "service_runtime.hpp"

#include "log_tags.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SERVICE_RUNTIME;
#define CM_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define CM_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define CM_LOGI(...) ((void)0)
#define CM_LOGW(...) ((void)0)
#endif

constexpr uint32_t kServiceZigbeePrimaryChannelMask = (1UL << 13);
constexpr uint8_t kServiceZigbeeMaxChildren = 16U;
constexpr uint8_t kAutoconnectRetryLimit = 5U;

bool is_deadline_reached(uint32_t now_ms, uint32_t deadline_ms) noexcept {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace

bool ConnectivityManager::ensure_wifi_mode_for_scan() noexcept {
    hal_wifi_mode_t mode = HAL_WIFI_MODE_NULL;
    if (hal_wifi_get_mode(&mode) != HAL_WIFI_STATUS_OK) {
        return false;
    }

    switch (mode) {
        case HAL_WIFI_MODE_STA:
        case HAL_WIFI_MODE_APSTA:
        case HAL_WIFI_MODE_AP:
            // Keep provisioning AP session stable during scan.
            // Switching AP -> APSTA can drop connected browser clients.
            return true;
        case HAL_WIFI_MODE_NULL:
            return hal_wifi_set_mode(HAL_WIFI_MODE_STA) == HAL_WIFI_STATUS_OK;
        default:
            return false;
    }
}

bool ConnectivityManager::ensure_wifi_mode_for_sta_connect() noexcept {
    hal_wifi_mode_t mode = HAL_WIFI_MODE_NULL;
    if (hal_wifi_get_mode(&mode) != HAL_WIFI_STATUS_OK) {
        return false;
    }

    switch (mode) {
        case HAL_WIFI_MODE_STA:
        case HAL_WIFI_MODE_APSTA:
            return true;
        case HAL_WIFI_MODE_AP:
            return hal_wifi_set_mode(HAL_WIFI_MODE_APSTA) == HAL_WIFI_STATUS_OK;
        case HAL_WIFI_MODE_NULL:
            return hal_wifi_set_mode(HAL_WIFI_MODE_STA) == HAL_WIFI_STATUS_OK;
        default:
            return false;
    }
}

bool ConnectivityManager::start_provisioning_ap(const char* ssid, const char* password) noexcept {
    if (ssid == nullptr || password == nullptr || ssid[0] == '\0') {
        return false;
    }
    return hal_wifi_start_ap(ssid, password) == HAL_WIFI_STATUS_OK;
}

ConnectivityAutoconnectResult ConnectivityManager::autoconnect_from_saved_credentials(ServiceRuntime& runtime) noexcept {
    const uint32_t now = runtime.monotonic_now_ms();
    if (next_autoconnect_attempt_ms_ != 0 && !is_deadline_reached(now, next_autoconnect_attempt_ms_)) {
        return ConnectivityAutoconnectResult::kConnectFailed;
    }

    if (autoconnect_retry_count_ >= kAutoconnectRetryLimit) {
        CM_LOGW("Auto-connect suppressed: reached max retries (%u)", kAutoconnectRetryLimit);
        runtime.stats_.current_backoff_ms.store(0U, std::memory_order_relaxed);
        return ConnectivityAutoconnectResult::kConnectFailed;
    }

    char ssid[33]{};
    if (hal_nvs_get_str("wifi_ssid", ssid, sizeof(ssid)) != HAL_NVS_STATUS_OK || ssid[0] == '\0') {
        CM_LOGI("Auto-connect skipped: saved Wi-Fi credentials not found");
        autoconnect_retry_count_ = kAutoconnectRetryLimit;
        runtime.stats_.current_backoff_ms.store(0U, std::memory_order_relaxed);
        return ConnectivityAutoconnectResult::kCredentialsMissing;
    }

    char password[65]{};
    if (hal_nvs_get_str("wifi_password", password, sizeof(password)) != HAL_NVS_STATUS_OK) {
        password[0] = '\0';
    }

    if (!ensure_wifi_mode_for_sta_connect()) {
        CM_LOGI("Auto-connect failed: unable to prepare Wi-Fi mode for STA connect");
        return ConnectivityAutoconnectResult::kConnectFailed;
    }

    // Keep provisioning UI stable on softAP. Background autoconnect attempts in
    // AP/APSTA can disrupt active HTTP sessions due to channel/mode transitions.
    hal_wifi_mode_t mode = HAL_WIFI_MODE_NULL;
    if (hal_wifi_get_mode(&mode) != HAL_WIFI_STATUS_OK) {
        CM_LOGW("Auto-connect skipped: unable to read Wi-Fi mode");
        return ConnectivityAutoconnectResult::kConnectFailed;
    }
    if (mode == HAL_WIFI_MODE_AP || mode == HAL_WIFI_MODE_APSTA) {
        size_t ap_client_count = 0U;
        const hal_wifi_status_t ap_client_status = hal_wifi_get_ap_client_count(&ap_client_count);
        if (ap_client_status != HAL_WIFI_STATUS_OK) {
            CM_LOGW("Auto-connect deferred in AP/APSTA: unable to read softAP station count");
            return ConnectivityAutoconnectResult::kConnectFailed;
        }
        if (ap_client_count > 0U) {
            CM_LOGI(
                "Auto-connect deferred in AP/APSTA while %u softAP client(s) are connected",
                static_cast<unsigned>(ap_client_count));
            return ConnectivityAutoconnectResult::kConnectFailed;
        }
    }

    if (hal_wifi_connect_sta(ssid, password) != HAL_WIFI_STATUS_OK) {
        const uint32_t delay_ms = (1UL << autoconnect_retry_count_) * 1000U;
        next_autoconnect_attempt_ms_ = now + delay_ms;
        runtime.stats_.current_backoff_ms.store(delay_ms, std::memory_order_relaxed);

        ++autoconnect_retry_count_;
        (void)runtime.stats_.autoconnect_failures.fetch_add(1, std::memory_order_relaxed);
        CM_LOGW(
            "Auto-connect failed (attempt %u), next retry in %lu ms",
            autoconnect_retry_count_,
            static_cast<unsigned long>(delay_ms));
        return ConnectivityAutoconnectResult::kConnectFailed;
    }

    autoconnect_retry_count_ = 0;
    next_autoconnect_attempt_ms_ = 0;
    runtime.stats_.current_backoff_ms.store(0U, std::memory_order_relaxed);
    mark_wifi_credentials_available();
    const bool zigbee_started_ok = ensure_zigbee_started(runtime);
#ifdef ESP_PLATFORM
    CM_LOGI(
        "Auto-connect: Wi-Fi connected, Zigbee %s",
        zigbee_started_ok ? "started" : "start failed");
#else
    (void)zigbee_started_ok;
#endif

    return ConnectivityAutoconnectResult::kConnectStarted;
}

bool ConnectivityManager::has_saved_wifi_credentials() noexcept {
    char ssid[33]{};
    return hal_nvs_get_str("wifi_ssid", ssid, sizeof(ssid)) == HAL_NVS_STATUS_OK && ssid[0] != '\0';
}

void ConnectivityManager::mark_wifi_credentials_available() noexcept {
    zigbee_start_allowed_ = true;
}

bool ConnectivityManager::ensure_zigbee_started(ServiceRuntime& runtime) noexcept {
    if (zigbee_started_) {
        return true;
    }

    if (!zigbee_start_allowed_) {
        return false;
    }

    const hal_zigbee_status_t channel_status =
        hal_zigbee_set_primary_channel_mask(kServiceZigbeePrimaryChannelMask);
    if (channel_status != HAL_ZIGBEE_STATUS_OK) {
        CM_LOGW(
            "Failed to apply Zigbee primary channel policy from service runtime (status=%d)",
            static_cast<int>(channel_status));
        return false;
    }
    const hal_zigbee_status_t children_status = hal_zigbee_set_max_children(kServiceZigbeeMaxChildren);
    if (children_status != HAL_ZIGBEE_STATUS_OK) {
        CM_LOGW(
            "Failed to apply Zigbee max-children policy from service runtime (status=%d)",
            static_cast<int>(children_status));
        return false;
    }

    const hal_zigbee_status_t init_status = hal_zigbee_init();
    if (init_status != HAL_ZIGBEE_STATUS_OK) {
        CM_LOGW("Failed to initialize Zigbee HAL from service runtime (status=%d)", static_cast<int>(init_status));
        return false;
    }

    zigbee_started_ = true;
    runtime.network_policy_manager_.on_zigbee_started(runtime, runtime.monotonic_now_ms());
    return true;
}

bool ConnectivityManager::zigbee_started() const noexcept {
    return zigbee_started_;
}

bool ConnectivityManager::can_attempt_autoconnect(uint32_t now_ms, uint8_t max_retries) const noexcept {
    if (autoconnect_retry_count_ >= max_retries) {
        return false;
    }

    if (next_autoconnect_attempt_ms_ == 0U) {
        return false;
    }

    return is_deadline_reached(now_ms, next_autoconnect_attempt_ms_);
}

void ConnectivityManager::clear_autoconnect_backoff(ServiceRuntime& runtime) noexcept {
    autoconnect_retry_count_ = 0;
    next_autoconnect_attempt_ms_ = 0U;
    runtime.stats_.current_backoff_ms.store(0U, std::memory_order_relaxed);
}

}  // namespace service
