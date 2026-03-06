/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "network_manager.hpp"

#include <cstring>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <thread>
#endif

#include "hal_nvs.h"
#include "hal_wifi.h"
#include "hal_zigbee.h"
#include "service_runtime.hpp"

namespace service {

NetworkManager::SpinLockGuard::SpinLockGuard(std::atomic_flag& lock) noexcept : lock_(lock) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
#ifdef ESP_PLATFORM
        taskYIELD();
#else
        std::this_thread::yield();
#endif
    }
}

NetworkManager::SpinLockGuard::~SpinLockGuard() noexcept {
    lock_.clear(std::memory_order_release);
}

bool NetworkManager::handle_event(const core::CoreEvent& event) noexcept {
    if (event.type != core::CoreEventType::kCommandRefreshNetworkRequested) {
        return false;
    }

    refresh_requested_ = true;
    ++refresh_count_;
    return true;
}

bool NetworkManager::refresh_requested() const noexcept {
    return refresh_requested_;
}

void NetworkManager::clear_refresh_requested() noexcept {
    refresh_requested_ = false;
}

uint32_t NetworkManager::refresh_count() const noexcept {
    return refresh_count_;
}

bool NetworkManager::enqueue_request(ServiceRuntime& runtime, const NetworkRequest& request) noexcept {
    if (request.request_id == 0 || request.operation == NetworkOperationType::kUnknown) {
        return false;
    }

    SpinLockGuard guard(queue_lock_);
    if (request_count_ >= request_queue_.size()) {
        (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    request_queue_[request_tail_] = request;
    request_tail_ = (request_tail_ + 1U) % request_queue_.size();
    ++request_count_;
    return true;
}

bool NetworkManager::pop_request(NetworkRequest* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || request_count_ == 0) {
        return false;
    }

    *out = request_queue_[request_head_];
    request_head_ = (request_head_ + 1U) % request_queue_.size();
    --request_count_;
    return true;
}

bool NetworkManager::drain_requests(ServiceRuntime& runtime) noexcept {
    bool drained = false;
    NetworkRequest request{};

    while (pop_request(&request)) {
        drained = true;
        bool queue_result = true;
        NetworkResult result{};
        if (!handle_request(runtime, request, &result, &queue_result)) {
            result = NetworkResult{};
            result.request_id = request.request_id;
            result.operation = request.operation;
            result.status = NetworkOperationStatus::kInvalidArgument;
            queue_result = true;
        }

        if (queue_result && !runtime.queue_network_result(result)) {
            (void)runtime.stats_.dropped_events.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return drained;
}

bool NetworkManager::is_scan_request_queued(uint32_t request_id) const noexcept {
    if (request_id == 0) {
        return false;
    }

    SpinLockGuard guard(queue_lock_);
    std::size_t index = request_head_;
    for (std::size_t i = 0; i < request_count_; ++i) {
        const NetworkRequest& pending_request = request_queue_[index];
        if (pending_request.request_id == request_id && pending_request.operation == NetworkOperationType::kScan) {
            return true;
        }
        index = (index + 1U) % request_queue_.size();
    }

    return false;
}

std::size_t NetworkManager::pending_ingress_count() const noexcept {
    SpinLockGuard guard(queue_lock_);
    return request_count_;
}

bool NetworkManager::handle_request(
    ServiceRuntime& runtime,
    const NetworkRequest& request,
    NetworkResult* result,
    bool* queue_result) noexcept {
    if (result == nullptr || queue_result == nullptr) {
        return false;
    }

    *result = NetworkResult{};
    result->request_id = request.request_id;
    result->operation = request.operation;
    result->status = NetworkOperationStatus::kOk;
    *queue_result = true;

    switch (request.operation) {
        case NetworkOperationType::kScan:
            return handle_scan(runtime, request, result, queue_result);
        case NetworkOperationType::kConnect:
            return handle_connect(runtime, request, result, queue_result);
        case NetworkOperationType::kCredentialsStatus:
            return handle_credentials_status(request, result);
        case NetworkOperationType::kCredentialsRawDebug:
            return handle_credentials_raw_debug(request, result);
        case NetworkOperationType::kOpenJoinWindow:
            return handle_open_join_window(runtime, request, result);
        case NetworkOperationType::kRemoveDevice:
            return handle_remove_device(runtime, request, result);
        case NetworkOperationType::kUnknown:
        default:
            result->status = NetworkOperationStatus::kInvalidArgument;
            return true;
    }
}

bool NetworkManager::handle_scan(
    ServiceRuntime& runtime,
    const NetworkRequest& request,
    NetworkResult* result,
    bool* queue_result) noexcept {
    if (runtime.scan_manager_.has_pending_or_busy()) {
        result->status = NetworkOperationStatus::kNoCapacity;
        return true;
    }

#ifdef ESP_PLATFORM
    if (!runtime.scan_manager_.enqueue_request(request.request_id)) {
        result->status = NetworkOperationStatus::kNoCapacity;
        return true;
    }
    *queue_result = false;
#else
    (void)request;
    (void)queue_result;
    if (!runtime.ensure_wifi_mode_for_scan()) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    hal_wifi_scan_record_t records[ServiceRuntime::kNetworkScanMaxRecords]{};
    size_t found_count = 0;
    if (hal_wifi_scan(records, ServiceRuntime::kNetworkScanMaxRecords, &found_count) != HAL_WIFI_STATUS_OK) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    if (found_count > ServiceRuntime::kNetworkScanMaxRecords) {
        found_count = ServiceRuntime::kNetworkScanMaxRecords;
    }
    result->scan_count = static_cast<uint8_t>(found_count);

    for (std::size_t i = 0; i < found_count; ++i) {
        std::strncpy(result->scan_records[i].ssid, records[i].ssid, sizeof(result->scan_records[i].ssid) - 1U);
        result->scan_records[i].rssi = records[i].rssi;
        result->scan_records[i].is_open = records[i].is_open;
    }
#endif

    return true;
}

bool NetworkManager::handle_connect(
    ServiceRuntime& runtime,
    const NetworkRequest& request,
    NetworkResult* result,
    bool* queue_result) noexcept {
    if (request.ssid[0] == '\0') {
        result->status = NetworkOperationStatus::kInvalidArgument;
        return true;
    }

    if (runtime.network_policy_manager_.has_pending_sta_connect()) {
        result->status = NetworkOperationStatus::kNoCapacity;
        return true;
    }

    if (runtime.scan_manager_.has_pending_or_busy()) {
        result->status = NetworkOperationStatus::kNoCapacity;
        return true;
    }

    if (request.save_credentials) {
        if (hal_nvs_set_str("wifi_ssid", request.ssid) != HAL_NVS_STATUS_OK ||
            hal_nvs_set_str("wifi_password", request.password) != HAL_NVS_STATUS_OK) {
            result->status = NetworkOperationStatus::kHalFailed;
            return true;
        }
        result->saved = true;
    }

    if (!runtime.ensure_wifi_mode_for_sta_connect()) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    runtime.connectivity_manager_.clear_autoconnect_backoff(runtime);

    if (hal_wifi_connect_sta_async(request.ssid, request.password) != HAL_WIFI_STATUS_OK) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    const uint32_t now_ms = runtime.monotonic_now_ms();
    runtime.network_policy_manager_.arm_pending_sta_connect(
        request.request_id,
        result->saved,
        request.ssid,
        now_ms + kStaConnectAsyncTimeoutMs);
    *queue_result = false;
    return true;
}

bool NetworkManager::handle_credentials_status(const NetworkRequest& request, NetworkResult* result) noexcept {
    (void)request;
    char ssid[33]{};
    char password[65]{};
    result->saved = hal_nvs_get_str("wifi_ssid", ssid, sizeof(ssid)) == HAL_NVS_STATUS_OK && ssid[0] != '\0';
    result->has_password =
        hal_nvs_get_str("wifi_password", password, sizeof(password)) == HAL_NVS_STATUS_OK && password[0] != '\0';
    if (result->saved) {
        std::strncpy(result->ssid, ssid, sizeof(result->ssid) - 1U);
    }
    return true;
}

bool NetworkManager::handle_credentials_raw_debug(const NetworkRequest& request, NetworkResult* result) noexcept {
    (void)request;
    char ssid[33]{};
    char password[65]{};
    const bool ssid_ok = hal_nvs_get_str("wifi_ssid", ssid, sizeof(ssid)) == HAL_NVS_STATUS_OK;
    const bool password_ok = hal_nvs_get_str("wifi_password", password, sizeof(password)) == HAL_NVS_STATUS_OK;

    result->debug_ssid_present = ssid_ok && ssid[0] != '\0';
    result->debug_password_present = password_ok && password[0] != '\0';
    result->debug_ssid_len = static_cast<uint8_t>(strnlen(ssid, sizeof(ssid)));
    result->debug_password_len = static_cast<uint8_t>(strnlen(password, sizeof(password)));

    if (result->debug_ssid_present) {
        std::strncpy(result->debug_ssid, ssid, sizeof(result->debug_ssid) - 1U);
        std::strncpy(result->ssid, ssid, sizeof(result->ssid) - 1U);
    }

    result->saved = result->debug_ssid_present;
    result->has_password = result->debug_password_present;
    return true;
}

bool NetworkManager::handle_open_join_window(
    ServiceRuntime& runtime,
    const NetworkRequest& request,
    NetworkResult* result) noexcept {
    if (request.join_window_seconds == 0U) {
        result->status = NetworkOperationStatus::kInvalidArgument;
        return true;
    }

    if (!runtime.ensure_zigbee_started()) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    if (!runtime.request_join_window_open(request.join_window_seconds)) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    result->join_window_seconds = request.join_window_seconds;
    return true;
}

bool NetworkManager::handle_remove_device(
    ServiceRuntime& runtime,
    const NetworkRequest& request,
    NetworkResult* result) noexcept {
    if (request.device_short_addr == core::kUnknownDeviceShortAddr || request.device_short_addr == 0x0000U) {
        result->status = NetworkOperationStatus::kInvalidArgument;
        return true;
    }

    result->force_remove = request.force_remove;
    result->force_remove_timeout_ms = request.force_remove_timeout_ms;

    const bool zigbee_ready = runtime.ensure_zigbee_started();
    if (!zigbee_ready && !request.force_remove) {
        result->status = NetworkOperationStatus::kHalFailed;
        return true;
    }

    if (zigbee_ready) {
        const hal_zigbee_status_t remove_status = hal_zigbee_remove_device(request.device_short_addr);
        if (remove_status != HAL_ZIGBEE_STATUS_OK && !request.force_remove) {
            result->status = NetworkOperationStatus::kHalFailed;
            return true;
        }
    }

    if (request.force_remove) {
        const uint32_t deadline_ms = runtime.monotonic_now_ms() + request.force_remove_timeout_ms;
        if (!runtime.schedule_force_remove(request.device_short_addr, deadline_ms)) {
            result->status = NetworkOperationStatus::kNoCapacity;
            return true;
        }
    }

    result->device_short_addr = request.device_short_addr;
    return true;
}

}  // namespace service
