/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "rcp_update_manager.hpp"

#include <cstring>

#include "hal_rcp.h"
#include "service_runtime.hpp"
#include "version.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr TickType_t kRcpWorkerIdleDelayTicks = pdMS_TO_TICKS(20);
#endif

template <std::size_t N>
void clear_chars(std::array<char, N>& value) noexcept {
    value.fill('\0');
}

template <std::size_t N>
void copy_chars(const char* source, std::array<char, N>& out) noexcept {
    clear_chars(out);
    if (source == nullptr || source[0] == '\0') {
        return;
    }

    std::strncpy(out.data(), source, out.size() - 1U);
    out.back() = '\0';
}

std::array<char, RcpUpdateResult::kVersionMaxLen> request_version_or_empty(const RcpUpdateRequest& request) noexcept {
    std::array<char, RcpUpdateResult::kVersionMaxLen> value{};
    copy_chars(request.target_version.data(), value);
    return value;
}

}  // namespace

RcpUpdateSubmitStatus RcpUpdateManager::enqueue_request(ServiceRuntime& runtime, const RcpUpdateRequest& request) noexcept {
    if (request.request_id == 0U || request.url[0] == '\0') {
        return RcpUpdateSubmitStatus::kInvalidRequest;
    }

    RuntimeLockGuard guard(queue_lock_);
    if (request_slot_.in_use || busy_.load(std::memory_order_acquire)) {
        return RcpUpdateSubmitStatus::kBusy;
    }

    request_slot_.request = request;
    request_slot_.in_use = true;
    pending_count_.store(1U, std::memory_order_release);
    runtime.note_rcp_update_poll_status(request.request_id, RcpUpdatePollStatus::kQueued);
    publish_status_queued(request);
    return RcpUpdateSubmitStatus::kAccepted;
}

bool RcpUpdateManager::build_api_snapshot(RcpUpdateApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    RuntimeLockGuard guard(snapshot_lock_);
    *out = snapshot_;
    copy_chars(common::kVersion, out->current_version);
    return true;
}

std::size_t RcpUpdateManager::pending_ingress_count() const noexcept {
    return static_cast<std::size_t>(pending_count_.load(std::memory_order_acquire));
}

bool RcpUpdateManager::has_pending_or_busy() const noexcept {
    return busy_.load(std::memory_order_acquire) || pending_count_.load(std::memory_order_acquire) != 0U;
}

bool RcpUpdateManager::pop_request(RcpUpdateRequest* out) noexcept {
    RuntimeLockGuard guard(queue_lock_);
    if (out == nullptr || !request_slot_.in_use) {
        return false;
    }

    *out = request_slot_.request;
    request_slot_.request = RcpUpdateRequest{};
    request_slot_.in_use = false;
    pending_count_.store(0U, std::memory_order_release);
    busy_.store(true, std::memory_order_release);
    return true;
}

void RcpUpdateManager::publish_status_queued(const RcpUpdateRequest& request) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kQueued;
    pending_status_update_.request_id = request.request_id;
    pending_status_update_.target_version = request_version_or_empty(request);
}

void RcpUpdateManager::publish_status_started(const RcpUpdateRequest& request) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kStarted;
    pending_status_update_.request_id = request.request_id;
    pending_status_update_.target_version = request_version_or_empty(request);
}

void RcpUpdateManager::publish_status_finished(const RcpUpdateResult& result) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kFinished;
    pending_status_update_.request_id = result.request_id;
    pending_status_update_.status = result.status;
    pending_status_update_.written_bytes = result.written_bytes;
    pending_status_update_.target_version = result.target_version;
}

bool RcpUpdateManager::take_status_update(StatusUpdate* out) noexcept {
    RuntimeLockGuard guard(update_lock_);
    if (out == nullptr || pending_status_update_.kind == StatusUpdateKind::kNone) {
        return false;
    }

    *out = pending_status_update_;
    pending_status_update_ = StatusUpdate{};
    return true;
}

bool RcpUpdateManager::apply_status_update(const StatusUpdate& update) noexcept {
    if (update.kind == StatusUpdateKind::kNone || update.request_id == 0U) {
        return false;
    }

    RuntimeLockGuard guard(snapshot_lock_);
    snapshot_.active_request_id = update.request_id;
    switch (update.kind) {
        case StatusUpdateKind::kQueued:
            snapshot_.busy = true;
            snapshot_.stage = RcpUpdateStage::kQueued;
            snapshot_.last_error = RcpUpdateOperationStatus::kOk;
            snapshot_.written_bytes = 0U;
            snapshot_.target_version = update.target_version;
            break;
        case StatusUpdateKind::kStarted:
            snapshot_.busy = true;
            snapshot_.stage = RcpUpdateStage::kApplying;
            snapshot_.last_error = RcpUpdateOperationStatus::kOk;
            snapshot_.written_bytes = 0U;
            snapshot_.target_version = update.target_version;
            break;
        case StatusUpdateKind::kFinished:
            snapshot_.busy = false;
            snapshot_.last_error = update.status;
            snapshot_.written_bytes = update.written_bytes;
            snapshot_.target_version = update.target_version;
            snapshot_.stage =
                (update.status == RcpUpdateOperationStatus::kOk) ? RcpUpdateStage::kCompleted : RcpUpdateStage::kFailed;
            break;
        case StatusUpdateKind::kNone:
        default:
            return false;
    }

    copy_chars(common::kVersion, snapshot_.current_version);
    return true;
}

bool RcpUpdateManager::process_request(ServiceRuntime& runtime, const RcpUpdateRequest& request) noexcept {
    runtime.note_rcp_update_poll_status(request.request_id, RcpUpdatePollStatus::kApplying);
    publish_status_started(request);

    RcpUpdateResult result{};
    result.request_id = request.request_id;
    result.target_version = request_version_or_empty(request);

    const int begin_rc = hal_rcp_update_begin();
    if (begin_rc != 0) {
        result.status = RcpUpdateOperationStatus::kBeginFailed;
        busy_.store(false, std::memory_order_release);
        publish_status_finished(result);
        return runtime.queue_rcp_update_result(result);
    }

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(request.url.data());
    const uint32_t payload_len = static_cast<uint32_t>(std::strlen(request.url.data()));
    const int write_rc = hal_rcp_update_write(payload, payload_len);
    if (write_rc != 0) {
        result.status = RcpUpdateOperationStatus::kWriteFailed;
        busy_.store(false, std::memory_order_release);
        publish_status_finished(result);
        return runtime.queue_rcp_update_result(result);
    }

    result.written_bytes = payload_len;
    const int end_rc = hal_rcp_update_end();
    result.status = (end_rc == 0) ? RcpUpdateOperationStatus::kOk : RcpUpdateOperationStatus::kFinalizeFailed;
    busy_.store(false, std::memory_order_release);
    publish_status_finished(result);
    return runtime.queue_rcp_update_result(result);
}

#ifdef ESP_PLATFORM
void RcpUpdateManager::worker_task_entry(void* arg) {
    auto* runtime = static_cast<ServiceRuntime*>(arg);
    if (runtime == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    runtime->rcp_update_manager_.run_worker_loop(*runtime);
}

void RcpUpdateManager::run_worker_loop(ServiceRuntime& runtime) noexcept {
    for (;;) {
        RcpUpdateRequest request{};
        if (!pop_request(&request)) {
            vTaskDelay(kRcpWorkerIdleDelayTicks);
            continue;
        }

        (void)process_request(runtime, request);
    }
}
#else
bool RcpUpdateManager::drain_requests(ServiceRuntime& runtime) noexcept {
    RcpUpdateRequest request{};
    if (!pop_request(&request)) {
        return false;
    }

    return process_request(runtime, request);
}
#endif

}  // namespace service
