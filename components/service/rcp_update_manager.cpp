/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "rcp_update_manager.hpp"

#include <cctype>
#include <cstring>

#include "hal_ota.h"
#include "hal_rcp.h"
#include "rcp_transport_policy.hpp"
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
constexpr const char* kExpectedRcpTransport = "uart";

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

bool is_empty_cstr(const char* value) noexcept {
    return value == nullptr || *value == '\0';
}

bool strings_equal(const char* lhs, const char* rhs) noexcept {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

bool is_valid_sha256_hex(const char* value) noexcept {
    if (value == nullptr || *value == '\0') {
        return true;
    }
    if (std::strlen(value) != 64U) {
        return false;
    }
    for (std::size_t i = 0; i < 64U; ++i) {
        if (std::isxdigit(static_cast<unsigned char>(value[i])) == 0) {
            return false;
        }
    }
    return true;
}

bool parse_dotted_numeric_version(const char* value, uint32_t* out_parts, std::size_t part_capacity, std::size_t* out_count) noexcept {
    if (value == nullptr || out_parts == nullptr || out_count == nullptr || part_capacity == 0U || *value == '\0') {
        return false;
    }

    std::size_t count = 0U;
    const char* cursor = value;
    while (*cursor != '\0') {
        if (count >= part_capacity) {
            return false;
        }
        if (std::isdigit(static_cast<unsigned char>(*cursor)) == 0) {
            return false;
        }

        uint32_t part = 0U;
        while (*cursor != '\0' && *cursor != '.') {
            if (std::isdigit(static_cast<unsigned char>(*cursor)) == 0) {
                return false;
            }
            part = (part * 10U) + static_cast<uint32_t>(*cursor - '0');
            ++cursor;
        }

        out_parts[count++] = part;
        if (*cursor == '.') {
            ++cursor;
            if (*cursor == '\0') {
                return false;
            }
        }
    }

    *out_count = count;
    return count != 0U;
}

bool version_is_older_than(const char* current, const char* minimum) noexcept {
    uint32_t current_parts[4]{};
    uint32_t minimum_parts[4]{};
    std::size_t current_count = 0U;
    std::size_t minimum_count = 0U;

    if (!parse_dotted_numeric_version(current, current_parts, 4U, &current_count) ||
        !parse_dotted_numeric_version(minimum, minimum_parts, 4U, &minimum_count)) {
        return false;
    }

    const std::size_t count = current_count > minimum_count ? current_count : minimum_count;
    for (std::size_t index = 0U; index < count; ++index) {
        const uint32_t lhs = index < current_count ? current_parts[index] : 0U;
        const uint32_t rhs = index < minimum_count ? minimum_parts[index] : 0U;
        if (lhs < rhs) {
            return true;
        }
        if (lhs > rhs) {
            return false;
        }
    }

    return false;
}

RcpUpdateSubmitStatus validate_request(const RcpUpdateRequest& request) noexcept {
    if (request.url[0] == '\0' || !is_valid_sha256_hex(request.sha256.data())) {
        return RcpUpdateSubmitStatus::kInvalidRequest;
    }
    if (!is_empty_cstr(request.board.data()) && !strings_equal(request.board.data(), common::kBoardId)) {
        return RcpUpdateSubmitStatus::kBoardMismatch;
    }
    if (!is_empty_cstr(request.transport.data()) && !strings_equal(request.transport.data(), kExpectedRcpTransport)) {
        return RcpUpdateSubmitStatus::kTransportMismatch;
    }
    if (!is_empty_cstr(request.gateway_min_version.data()) &&
        version_is_older_than(common::kVersion, request.gateway_min_version.data())) {
        return RcpUpdateSubmitStatus::kGatewayVersionMismatch;
    }
    return RcpUpdateSubmitStatus::kAccepted;
}

RcpUpdateOperationStatus map_hal_status(hal_rcp_https_status_t status) noexcept {
    switch (status) {
        case HAL_RCP_HTTPS_STATUS_OK:
            return RcpUpdateOperationStatus::kOk;
        case HAL_RCP_HTTPS_STATUS_INVALID_ARGUMENT:
            return RcpUpdateOperationStatus::kInvalidArgument;
        case HAL_RCP_HTTPS_STATUS_TRANSPORT_FAILED:
            return RcpUpdateOperationStatus::kTransportFailed;
        case HAL_RCP_HTTPS_STATUS_VERIFY_FAILED:
            return RcpUpdateOperationStatus::kVerifyFailed;
        case HAL_RCP_HTTPS_STATUS_APPLY_FAILED:
            return RcpUpdateOperationStatus::kApplyFailed;
        case HAL_RCP_HTTPS_STATUS_PROBE_FAILED:
            return RcpUpdateOperationStatus::kProbeFailed;
        case HAL_RCP_HTTPS_STATUS_RECOVERY_FAILED:
            return RcpUpdateOperationStatus::kRecoveryFailed;
        case HAL_RCP_HTTPS_STATUS_INTERNAL_ERROR:
        default:
            return RcpUpdateOperationStatus::kInternalError;
    }
}

void fill_backend_snapshot_fields(RcpUpdateApiSnapshot* snapshot) noexcept {
    if (snapshot == nullptr) {
        return;
    }

    snapshot->backend_available = hal_rcp_backend_available();
    snapshot->backend_name.fill('\0');
    if (!hal_rcp_get_backend_name(snapshot->backend_name.data(), snapshot->backend_name.size())) {
        copy_chars(snapshot->backend_available ? "available" : "unconfigured", snapshot->backend_name);
    }
}

}  // namespace

RcpUpdateSubmitStatus RcpUpdateManager::enqueue_request(ServiceRuntime& runtime, const RcpUpdateRequest& request) noexcept {
    if (request.request_id == 0U || request.url[0] == '\0') {
        return RcpUpdateSubmitStatus::kInvalidRequest;
    }

    const RcpUpdateSubmitStatus validation_status = validate_request(request);
    if (validation_status != RcpUpdateSubmitStatus::kAccepted) {
        return validation_status;
    }
    if (!hal_rcp_backend_available()) {
        return RcpUpdateSubmitStatus::kUnsupportedBackend;
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
    fill_backend_snapshot_fields(out);
    if (!hal_rcp_get_running_version(out->current_version.data(), out->current_version.size())) {
        copy_chars(common::kVersion, out->current_version);
    }
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

    if (!hal_rcp_get_running_version(snapshot_.current_version.data(), snapshot_.current_version.size())) {
        copy_chars(common::kVersion, snapshot_.current_version);
    }
    fill_backend_snapshot_fields(&snapshot_);
    return true;
}

bool RcpUpdateManager::process_request(ServiceRuntime& runtime, const RcpUpdateRequest& request) noexcept {
    runtime.note_rcp_update_poll_status(request.request_id, RcpUpdatePollStatus::kApplying);
    publish_status_started(request);

    RcpUpdateResult result{};
    result.request_id = request.request_id;
    result.target_version = request_version_or_empty(request);
    if (!hal_rcp_backend_available()) {
        result.status = RcpUpdateOperationStatus::kUnsupportedBackend;
        busy_.store(false, std::memory_order_release);
        publish_status_finished(result);
        return runtime.queue_rcp_update_result(result);
    }
    hal_rcp_https_request_t hal_request{};
    if (!build_rcp_transport_request(request, &hal_request)) {
        result.status = RcpUpdateOperationStatus::kInvalidArgument;
        busy_.store(false, std::memory_order_release);
        publish_status_finished(result);
        return runtime.queue_rcp_update_result(result);
    }

    hal_rcp_https_result_t hal_result{};
    const int rc = hal_rcp_perform_https_update(&hal_request, &hal_result);
    result.written_bytes = hal_result.bytes_read;
    result.status = map_hal_status(hal_result.status);
    if (rc == 0 && hal_result.discovered_version[0] != '\0') {
        copy_chars(hal_result.discovered_version, result.target_version);
    }
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
