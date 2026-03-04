/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "scan_manager.hpp"

#include <cstring>

#include "hal_wifi.h"
#include "log_tags.h"
#include "service_runtime.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr TickType_t kScanWorkerIdleDelayTicks = pdMS_TO_TICKS(20);
constexpr const char* kTag = LOG_TAG_SERVICE_RUNTIME;
#define SCAN_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define SCAN_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define SCAN_LOGI(...) ((void)0)
#define SCAN_LOGW(...) ((void)0)
#endif

}  // namespace

ScanManager::SpinLockGuard::SpinLockGuard(std::atomic_flag& lock) noexcept : lock_(lock) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
}

ScanManager::SpinLockGuard::~SpinLockGuard() noexcept {
    lock_.clear(std::memory_order_release);
}

bool ScanManager::enqueue_request(uint32_t request_id) noexcept {
    if (request_id == 0U) {
        return false;
    }

    SpinLockGuard guard(queue_lock_);
    if (queue_count_ >= queue_.size()) {
        return false;
    }

    queue_[queue_tail_].request_id = request_id;
    queue_tail_ = (queue_tail_ + 1U) % queue_.size();
    ++queue_count_;
    pending_count_.store(static_cast<uint32_t>(queue_count_), std::memory_order_release);
    return true;
}

bool ScanManager::pop_request(Request* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || queue_count_ == 0U) {
        return false;
    }

    *out = queue_[queue_head_];
    queue_head_ = (queue_head_ + 1U) % queue_.size();
    --queue_count_;
    pending_count_.store(static_cast<uint32_t>(queue_count_), std::memory_order_release);
    return true;
}

bool ScanManager::pop_request_for_test(uint32_t* request_id) noexcept {
    if (request_id == nullptr) {
        return false;
    }

    Request request{};
    if (!pop_request(&request)) {
        return false;
    }

    *request_id = request.request_id;
    return true;
}

bool ScanManager::is_request_queued(uint32_t request_id) const noexcept {
    if (request_id == 0U) {
        return false;
    }

    SpinLockGuard guard(queue_lock_);
    std::size_t index = queue_head_;
    for (std::size_t i = 0; i < queue_count_; ++i) {
        if (queue_[index].request_id == request_id) {
            return true;
        }
        index = (index + 1U) % queue_.size();
    }

    return false;
}

bool ScanManager::is_request_in_progress(uint32_t request_id) const noexcept {
    if (request_id == 0U) {
        return false;
    }

    return active_request_id_.load(std::memory_order_acquire) == request_id;
}

void ScanManager::set_request_in_progress_for_test(uint32_t request_id) noexcept {
    active_request_id_.store(request_id, std::memory_order_release);
    busy_.store(request_id != 0U, std::memory_order_release);
}

void ScanManager::clear_request_in_progress_for_test() noexcept {
    active_request_id_.store(0U, std::memory_order_release);
    busy_.store(false, std::memory_order_release);
}

std::size_t ScanManager::pending_ingress_count() const noexcept {
    return static_cast<std::size_t>(pending_count_.load(std::memory_order_acquire));
}

bool ScanManager::has_pending_or_busy() const noexcept {
    return busy_.load(std::memory_order_acquire) || pending_count_.load(std::memory_order_acquire) != 0U;
}

#ifdef ESP_PLATFORM
void ScanManager::worker_task_entry(void* arg) {
    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(arg);
    if (runtime == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    runtime->scan_manager_.run_worker_loop(*runtime);
}

void ScanManager::run_worker_loop(ServiceRuntime& runtime) noexcept {
    const TickType_t idle_delay_ticks = kScanWorkerIdleDelayTicks > 0 ? kScanWorkerIdleDelayTicks : 1U;

    for (;;) {
        Request request{};
        if (!pop_request(&request)) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        SCAN_LOGI("Scan worker picked request_id=%lu", static_cast<unsigned long>(request.request_id));
        busy_.store(true, std::memory_order_release);
        active_request_id_.store(request.request_id, std::memory_order_release);

        NetworkResult result{};
        result.request_id = request.request_id;
        result.operation = NetworkOperationType::kScan;
        result.status = NetworkOperationStatus::kOk;

        if (!runtime.ensure_wifi_mode_for_scan()) {
            result.status = NetworkOperationStatus::kHalFailed;
        } else {
            hal_wifi_scan_record_t records[kNetworkScanMaxRecords]{};
            size_t found_count = 0;
            if (hal_wifi_scan(records, kNetworkScanMaxRecords, &found_count) != HAL_WIFI_STATUS_OK) {
                result.status = NetworkOperationStatus::kHalFailed;
            } else {
                if (found_count > kNetworkScanMaxRecords) {
                    found_count = kNetworkScanMaxRecords;
                }
                result.scan_count = static_cast<uint8_t>(found_count);
                for (std::size_t i = 0; i < found_count; ++i) {
                    std::strncpy(
                        result.scan_records[i].ssid,
                        records[i].ssid,
                        sizeof(result.scan_records[i].ssid) - 1U);
                    result.scan_records[i].rssi = records[i].rssi;
                    result.scan_records[i].is_open = records[i].is_open;
                }
            }
        }

        busy_.store(false, std::memory_order_release);
        active_request_id_.store(0U, std::memory_order_release);
        if (!runtime.queue_network_result(result)) {
            (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
            SCAN_LOGW("Scan result dropped request_id=%lu", static_cast<unsigned long>(request.request_id));
        } else {
            SCAN_LOGI(
                "Scan result queued request_id=%lu status=%u count=%u",
                static_cast<unsigned long>(request.request_id),
                static_cast<unsigned>(result.status),
                static_cast<unsigned>(result.scan_count));
        }
    }
}
#endif

}  // namespace service
