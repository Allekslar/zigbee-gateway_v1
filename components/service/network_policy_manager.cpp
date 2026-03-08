/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "network_policy_manager.hpp"

#include <cstring>

#include "hal_zigbee.h"
#include "service_runtime.hpp"

namespace service {

bool NetworkPolicyManager::is_deadline_reached(uint32_t now_ms, uint32_t deadline_ms) noexcept {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool NetworkPolicyManager::has_pending_sta_connect() const noexcept {
    return pending_sta_connect_.in_use;
}

void NetworkPolicyManager::arm_pending_sta_connect(
    uint32_t request_id,
    bool saved,
    const char* ssid,
    uint32_t deadline_ms) noexcept {
    pending_sta_connect_ = PendingStaConnect{};
    pending_sta_connect_.in_use = true;
    pending_sta_connect_.saved = saved;
    pending_sta_connect_.request_id = request_id;
    pending_sta_connect_.deadline_ms = deadline_ms;
    if (ssid != nullptr) {
        std::strncpy(pending_sta_connect_.ssid, ssid, sizeof(pending_sta_connect_.ssid) - 1U);
    }
}

std::size_t NetworkPolicyManager::process_pending_sta_connect(
    ServiceRuntime& runtime,
    bool connected,
    uint32_t now_ms) noexcept {
    if (!pending_sta_connect_.in_use) {
        return 0;
    }

    if (!connected && !is_deadline_reached(now_ms, pending_sta_connect_.deadline_ms)) {
        return 0;
    }

    NetworkResult result{};
    result.request_id = pending_sta_connect_.request_id;
    result.operation = NetworkOperationType::kConnect;
    result.saved = pending_sta_connect_.saved;
    std::strncpy(result.ssid, pending_sta_connect_.ssid, sizeof(result.ssid) - 1U);
    result.status = connected ? NetworkOperationStatus::kOk : NetworkOperationStatus::kHalFailed;
    pending_sta_connect_ = PendingStaConnect{};

    if (connected) {
        runtime.mark_wifi_credentials_available();
        (void)runtime.ensure_zigbee_started();
    }

    if (!runtime.queue_network_result(result)) {
        runtime.note_dropped_event();
        return 0;
    }
    return 1;
}

bool NetworkPolicyManager::request_join_window_open(
    ServiceRuntime& runtime,
    uint16_t duration_seconds,
    uint32_t now_ms) noexcept {
    if (duration_seconds == 0U) {
        return false;
    }

    join_window_explicit_expected_ = true;

    const hal_zigbee_status_t open_err = hal_zigbee_open_network(duration_seconds);
    if (open_err == HAL_ZIGBEE_STATUS_OK) {
        pending_join_window_seconds_ = 0U;
        runtime.set_join_window_cache(true, duration_seconds);
        return true;
    }

    if (open_err != HAL_ZIGBEE_STATUS_NETWORK_NOT_FORMED) {
        join_window_explicit_expected_ = false;
        return false;
    }

    pending_join_window_seconds_ = duration_seconds;
    const hal_zigbee_status_t formation_status = hal_zigbee_start_network_formation();
    if (formation_status == HAL_ZIGBEE_STATUS_OK) {
        ++zigbee_formation_retry_count_;
    }
    zigbee_next_formation_retry_ms_ = now_ms + kZigbeeFormationRetryMs;
    return true;
}

void NetworkPolicyManager::maybe_request_auto_rejoin_window(
    ServiceRuntime& runtime,
    core::CoreEventType event_type,
    uint16_t short_addr,
    uint32_t now_ms) noexcept {
    if (event_type != core::CoreEventType::kCommandResultFailed &&
        event_type != core::CoreEventType::kCommandResultTimeout) {
        return;
    }

    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0U) {
        return;
    }

    if (auto_rejoin_next_open_ms_ != 0U && !is_deadline_reached(now_ms, auto_rejoin_next_open_ms_)) {
        return;
    }

    uint16_t seconds_left = 0U;
    if (runtime.get_join_window_status(&seconds_left) && seconds_left > 0U) {
        auto_rejoin_next_open_ms_ = now_ms + kAutoRejoinCooldownMs;
        return;
    }

    if (request_join_window_open(runtime, kAutoRejoinWindowSeconds, now_ms)) {
        auto_rejoin_next_open_ms_ = now_ms + kAutoRejoinCooldownMs;
    }
}

void NetworkPolicyManager::process_zigbee_join_window_policy(ServiceRuntime& runtime, uint32_t now_ms) noexcept {
    if (!runtime.zigbee_started()) {
        runtime.set_join_window_cache(false, 0U);
        return;
    }

    // Factory-new coordinator bootstrap: on_zigbee_started() can race with
    // async Zigbee stack task startup and return NOT_STARTED once.
    // Keep retrying formation until network is formed.
    if (pending_join_window_seconds_ == 0U && !hal_zigbee_is_network_formed() &&
        (zigbee_next_formation_retry_ms_ == 0U || is_deadline_reached(now_ms, zigbee_next_formation_retry_ms_))) {
        const hal_zigbee_status_t formation_status = hal_zigbee_start_network_formation();
        if (formation_status == HAL_ZIGBEE_STATUS_OK) {
            ++zigbee_formation_retry_count_;
        }
        zigbee_next_formation_retry_ms_ = now_ms + kZigbeeFormationRetryMs;
    }

    if (pending_join_window_seconds_ > 0U && !hal_zigbee_is_network_formed()) {
        if (zigbee_next_formation_retry_ms_ == 0U || is_deadline_reached(now_ms, zigbee_next_formation_retry_ms_)) {
            const hal_zigbee_status_t formation_status = hal_zigbee_start_network_formation();
            if (formation_status == HAL_ZIGBEE_STATUS_OK) {
                ++zigbee_formation_retry_count_;
            }
            zigbee_next_formation_retry_ms_ = now_ms + kZigbeeFormationRetryMs;
        }
    }

    if (pending_join_window_seconds_ > 0U && hal_zigbee_is_network_formed()) {
        const uint16_t duration_seconds = pending_join_window_seconds_;
        const hal_zigbee_status_t open_err = hal_zigbee_open_network(duration_seconds);
        if (open_err == HAL_ZIGBEE_STATUS_OK) {
            pending_join_window_seconds_ = 0U;
            join_window_explicit_expected_ = true;
            zigbee_next_formation_retry_ms_ = 0U;
            zigbee_formation_retry_count_ = 0U;
        } else {
            zigbee_next_formation_retry_ms_ = now_ms + kZigbeeFormationRetryMs;
        }
    }

    bool join_open = false;
    uint16_t seconds_left = 0U;
    if (hal_zigbee_get_join_window_status(&join_open, &seconds_left) != HAL_ZIGBEE_STATUS_OK) {
        runtime.set_join_window_cache(false, 0U);
        return;
    }

    runtime.set_join_window_cache(join_open, seconds_left);

    if (!join_open) {
        zigbee_join_window_was_open_ = false;
        if (pending_join_window_seconds_ == 0U) {
            join_window_explicit_expected_ = false;
        }
        return;
    }

    // Allow short implicit windows (auto-rejoin), but close unexpectedly long
    // implicit windows (e.g. stack default 180s) to avoid prolonged exposure.
    if (!join_window_explicit_expected_ && pending_join_window_seconds_ == 0U && seconds_left > 90U) {
        if (hal_zigbee_close_network() == HAL_ZIGBEE_STATUS_OK) {
            runtime.set_join_window_cache(false, 0U);
            zigbee_join_window_was_open_ = false;
            return;
        }
    }

    zigbee_join_window_was_open_ = true;
}

void NetworkPolicyManager::on_zigbee_started(ServiceRuntime& runtime, uint32_t now_ms) noexcept {
    runtime.set_join_window_cache(false, 0U);

    if (hal_zigbee_is_network_formed()) {
        zigbee_next_formation_retry_ms_ = 0U;
        zigbee_formation_retry_count_ = 0U;
        return;
    }

    const hal_zigbee_status_t formation_status = hal_zigbee_start_network_formation();
    zigbee_next_formation_retry_ms_ = now_ms + kZigbeeFormationRetryMs;
    zigbee_formation_retry_count_ = (formation_status == HAL_ZIGBEE_STATUS_OK) ? 1U : 0U;
}

void NetworkPolicyManager::on_join_window_force_closed(ServiceRuntime& runtime) noexcept {
    join_window_explicit_expected_ = false;
    pending_join_window_seconds_ = 0U;
    runtime.set_join_window_cache(false, 0U);
}

}  // namespace service
