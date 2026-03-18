/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "ota_manager.hpp"

#include <cstring>

#include "hal_ota.h"
#include "service_runtime.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr TickType_t kOtaWorkerIdleDelayTicks = pdMS_TO_TICKS(20);
#endif

template <std::size_t N>
void clear_chars(std::array<char, N>& value) noexcept {
    value.fill('\0');
}

template <std::size_t N>
bool copy_chars(const char* source, std::array<char, N>& out) noexcept {
    clear_chars(out);
    if (source == nullptr) {
        return false;
    }

    const std::size_t source_len = std::strlen(source);
    if (source_len >= out.size()) {
        return false;
    }

    std::memcpy(out.data(), source, source_len);
    out[source_len] = '\0';
    return true;
}

OtaOperationStatus map_hal_status(hal_ota_https_status_t status) noexcept {
    switch (status) {
        case HAL_OTA_HTTPS_STATUS_OK:
            return OtaOperationStatus::kOk;
        case HAL_OTA_HTTPS_STATUS_INVALID_ARGUMENT:
            return OtaOperationStatus::kInvalidArgument;
        case HAL_OTA_HTTPS_STATUS_DOWNLOAD_FAILED:
            return OtaOperationStatus::kDownloadFailed;
        case HAL_OTA_HTTPS_STATUS_VERIFY_FAILED:
            return OtaOperationStatus::kVerifyFailed;
        case HAL_OTA_HTTPS_STATUS_APPLY_FAILED:
            return OtaOperationStatus::kApplyFailed;
        case HAL_OTA_HTTPS_STATUS_INTERNAL_ERROR:
        default:
            return OtaOperationStatus::kInternalError;
    }
}

}  // namespace

bool OtaManager::enqueue_request(ServiceRuntime& runtime, const OtaStartRequest& request) noexcept {
    if (request.request_id == 0U || request.url[0] == '\0') {
        return false;
    }

    {
        RuntimeLockGuard guard(queue_lock_);
        if (request_slot_.in_use || busy_.load(std::memory_order_acquire)) {
            return false;
        }

        request_slot_.request = request;
        request_slot_.in_use = true;
        pending_count_.store(1U, std::memory_order_release);
    }

    runtime.note_ota_poll_status(request.request_id, OtaPollStatus::kQueued);
    publish_status_queued(request);
    return true;
}

bool OtaManager::build_api_snapshot(OtaApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    {
        RuntimeLockGuard guard(snapshot_lock_);
        *out = snapshot_;
    }

    clear_chars(out->current_version);
    (void)hal_ota_get_running_version(out->current_version.data(), out->current_version.size());
    return true;
}

std::size_t OtaManager::pending_ingress_count() const noexcept {
    return static_cast<std::size_t>(pending_count_.load(std::memory_order_acquire));
}

bool OtaManager::has_pending_or_busy() const noexcept {
    return busy_.load(std::memory_order_acquire) || pending_count_.load(std::memory_order_acquire) != 0U;
}

bool OtaManager::pop_request(OtaStartRequest* out) noexcept {
    RuntimeLockGuard guard(queue_lock_);
    if (out == nullptr || !request_slot_.in_use) {
        return false;
    }

    *out = request_slot_.request;
    request_slot_.request = OtaStartRequest{};
    request_slot_.in_use = false;
    pending_count_.store(0U, std::memory_order_release);
    busy_.store(true, std::memory_order_release);
    active_request_id_.store(out->request_id, std::memory_order_release);
    return true;
}

void OtaManager::publish_status_queued(const OtaStartRequest& request) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kQueued;
    pending_status_update_.request_id = request.request_id;
    pending_status_update_.target_version = request.target_version;
}

void OtaManager::publish_status_started(const OtaStartRequest& request) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kStarted;
    pending_status_update_.request_id = request.request_id;
    pending_status_update_.target_version = request.target_version;
}

void OtaManager::publish_status_progress(
    uint32_t request_id,
    uint32_t bytes_read,
    uint32_t image_size,
    bool image_size_known) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kProgress;
    pending_status_update_.request_id = request_id;
    pending_status_update_.downloaded_bytes = bytes_read;
    pending_status_update_.image_size = image_size;
    pending_status_update_.image_size_known = image_size_known;
}

void OtaManager::publish_status_finished(const OtaResult& result) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kFinished;
    pending_status_update_.request_id = result.request_id;
    pending_status_update_.status = result.status;
    pending_status_update_.downloaded_bytes = result.downloaded_bytes;
    pending_status_update_.image_size = result.image_size;
    pending_status_update_.image_size_known = result.image_size_known;
    pending_status_update_.target_version = result.target_version;
}

void OtaManager::publish_status_reboot_pending(uint32_t request_id) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kRebootPending;
    pending_status_update_.request_id = request_id;
}

bool OtaManager::take_status_update(StatusUpdate* out) noexcept {
    RuntimeLockGuard guard(update_lock_);
    if (out == nullptr || pending_status_update_.kind == StatusUpdateKind::kNone) {
        return false;
    }

    *out = pending_status_update_;
    pending_status_update_ = StatusUpdate{};
    return true;
}

bool OtaManager::apply_status_update(const StatusUpdate& update) noexcept {
    if (update.kind == StatusUpdateKind::kNone || update.request_id == 0U) {
        return false;
    }

    RuntimeLockGuard guard(snapshot_lock_);
    snapshot_.active_request_id = update.request_id;

    switch (update.kind) {
        case StatusUpdateKind::kQueued:
            snapshot_.busy = true;
            snapshot_.stage = OtaStage::kQueued;
            snapshot_.last_error = OtaOperationStatus::kOk;
            snapshot_.downloaded_bytes = 0U;
            snapshot_.image_size = 0U;
            snapshot_.image_size_known = false;
            snapshot_.target_version = update.target_version;
            return true;
        case StatusUpdateKind::kStarted:
            snapshot_.busy = true;
            snapshot_.stage = OtaStage::kDownloading;
            snapshot_.last_error = OtaOperationStatus::kOk;
            snapshot_.downloaded_bytes = 0U;
            snapshot_.image_size = 0U;
            snapshot_.image_size_known = false;
            snapshot_.target_version = update.target_version;
            return true;
        case StatusUpdateKind::kProgress:
            snapshot_.busy = true;
            snapshot_.stage = OtaStage::kDownloading;
            snapshot_.downloaded_bytes = update.downloaded_bytes;
            snapshot_.image_size = update.image_size;
            snapshot_.image_size_known = update.image_size_known;
            return true;
        case StatusUpdateKind::kFinished:
            snapshot_.busy = false;
            snapshot_.stage = (update.status == OtaOperationStatus::kOk) ? OtaStage::kSwitchPending : OtaStage::kFailed;
            snapshot_.last_error = (update.status == OtaOperationStatus::kOk) ? OtaOperationStatus::kOk : update.status;
            snapshot_.downloaded_bytes = update.downloaded_bytes;
            snapshot_.image_size = update.image_size;
            snapshot_.image_size_known = update.image_size_known;
            snapshot_.target_version = update.target_version;
            return true;
        case StatusUpdateKind::kRebootPending:
            if (snapshot_.stage == OtaStage::kSwitchPending) {
                snapshot_.stage = OtaStage::kRebootPending;
            }
            return true;
        case StatusUpdateKind::kNone:
        default:
            return false;
    }
}

void OtaManager::progress_callback(
    uint32_t bytes_read,
    uint32_t image_size,
    bool image_size_known,
    void* user_ctx) noexcept {
    auto* context = static_cast<ProgressContext*>(user_ctx);
    if (context == nullptr || context->manager == nullptr || context->request_id == 0U) {
        return;
    }

    context->manager->publish_status_progress(context->request_id, bytes_read, image_size, image_size_known);
}

bool OtaManager::process_request(ServiceRuntime& runtime, const OtaStartRequest& request) noexcept {
    publish_status_started(request);
    runtime.note_ota_poll_status(request.request_id, OtaPollStatus::kDownloading);

    hal_ota_https_request_t hal_request{};
    hal_request.url = request.url.data();
    hal_request.expected_version = request.target_version[0] != '\0' ? request.target_version.data() : nullptr;

    hal_ota_https_result_t hal_result{};
    ProgressContext progress_context{};
    progress_context.manager = this;
    progress_context.request_id = request.request_id;

    const int hal_rc = hal_ota_perform_https_update(
        &hal_request,
        &OtaManager::progress_callback,
        &progress_context,
        &hal_result);

    OtaResult result{};
    result.request_id = request.request_id;
    result.status = map_hal_status(hal_result.status);
    result.reboot_required = hal_result.reboot_required;
    result.downloaded_bytes = hal_result.bytes_read;
    result.image_size = hal_result.image_size;
    result.image_size_known = hal_result.image_size_known;
    result.target_version = request.target_version;

    if (result.target_version[0] == '\0' && hal_result.discovered_version[0] != '\0') {
        (void)copy_chars(hal_result.discovered_version, result.target_version);
    }

    if (hal_rc != 0 && result.status == OtaOperationStatus::kOk) {
        result.status = OtaOperationStatus::kInternalError;
    }

    publish_status_finished(result);
    busy_.store(false, std::memory_order_release);
    active_request_id_.store(0U, std::memory_order_release);

    const bool queued = runtime.queue_ota_result(result);

#ifdef ESP_PLATFORM
#if CONFIG_ZGW_OTA_AUTO_REBOOT_ENABLED
    if (result.status == OtaOperationStatus::kOk && result.reboot_required) {
        publish_status_reboot_pending(result.request_id);
        (void)hal_ota_schedule_restart(static_cast<uint32_t>(CONFIG_ZGW_OTA_REBOOT_DELAY_MS));
    }
#endif
#endif

    return queued;
}

#ifdef ESP_PLATFORM
void OtaManager::worker_task_entry(void* arg) {
    ServiceRuntime* runtime = static_cast<ServiceRuntime*>(arg);
    if (runtime == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    runtime->ota_manager_.run_worker_loop(*runtime);
}

void OtaManager::run_worker_loop(ServiceRuntime& runtime) noexcept {
    const TickType_t idle_delay_ticks = kOtaWorkerIdleDelayTicks > 0 ? kOtaWorkerIdleDelayTicks : 1U;

    for (;;) {
        OtaStartRequest request{};
        if (!pop_request(&request)) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        if (!process_request(runtime, request)) {
            runtime.note_dropped_event();
        }
    }
}
#else
bool OtaManager::drain_requests(ServiceRuntime& runtime) noexcept {
    OtaStartRequest request{};
    if (!pop_request(&request)) {
        return false;
    }

    if (!process_request(runtime, request)) {
        runtime.note_dropped_event();
    }
    return true;
}
#endif

}  // namespace service
