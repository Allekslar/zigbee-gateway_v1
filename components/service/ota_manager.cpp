/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "ota_manager.hpp"

#include <inttypes.h>
#include <cstring>

#include "hal_mqtt.h"
#include "hal_ota.h"
#include "log_tags.h"
#include "service_runtime.hpp"
#include "version.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#endif

#ifndef CONFIG_ZGW_MQTT_RESUME_AFTER_OTA_DELAY_MS
#define CONFIG_ZGW_MQTT_RESUME_AFTER_OTA_DELAY_MS 0
#endif

namespace service {

namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SERVICE_RUNTIME;
constexpr TickType_t kOtaWorkerIdleDelayTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kOtaWorkerPreflightDelayTicks = pdMS_TO_TICKS(250);
constexpr TickType_t kOtaWorkerMqttResumeDelayTicks = pdMS_TO_TICKS(CONFIG_ZGW_MQTT_RESUME_AFTER_OTA_DELAY_MS);
#define OTA_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define OTA_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define OTA_LOGI(...) ((void)0)
#define OTA_LOGW(...) ((void)0)
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

OtaManifestContext build_manifest_context(ServiceRuntime& runtime) noexcept {
    OtaManifestContext context{};
    (void)hal_ota_get_running_version(context.current_version.data(), context.current_version.size());
    std::strncpy(context.current_project.data(), common::kProjectName, context.current_project.size() - 1U);
    std::strncpy(context.current_board.data(), common::kBoardId, context.current_board.size() - 1U);
    std::strncpy(context.current_chip_target.data(), common::kChipTarget, context.current_chip_target.size() - 1U);
    context.current_schema = runtime.config_manager().schema_version();
    return context;
}

std::array<char, OtaApiSnapshot::kVersionMaxLen> manifest_version_or_empty(const OtaManifest& manifest) noexcept {
    std::array<char, OtaApiSnapshot::kVersionMaxLen> value{};
    const std::size_t max_copy = value.size() - 1U;
    std::strncpy(value.data(), manifest.version.data(), max_copy);
    value[max_copy] = '\0';
    return value;
}

void apply_manifest_defaults_to_request(const OtaManifestContext& context, OtaStartRequest* request) noexcept {
    if (request == nullptr) {
        return;
    }
    apply_ota_manifest_defaults(context, &request->manifest);
}

OtaSubmitStatus map_manifest_status(OtaManifestValidationStatus status) noexcept {
    switch (status) {
        case OtaManifestValidationStatus::kOk:
            return OtaSubmitStatus::kAccepted;
        case OtaManifestValidationStatus::kInvalidUrl:
        case OtaManifestValidationStatus::kInvalidSha256:
            return OtaSubmitStatus::kInvalidManifest;
        case OtaManifestValidationStatus::kProjectMismatch:
            return OtaSubmitStatus::kProjectMismatch;
        case OtaManifestValidationStatus::kBoardMismatch:
            return OtaSubmitStatus::kBoardMismatch;
        case OtaManifestValidationStatus::kChipTargetMismatch:
            return OtaSubmitStatus::kChipTargetMismatch;
        case OtaManifestValidationStatus::kSchemaTooNew:
            return OtaSubmitStatus::kSchemaMismatch;
        case OtaManifestValidationStatus::kDowngradeRejected:
            return OtaSubmitStatus::kDowngradeRejected;
        default:
            return OtaSubmitStatus::kInvalidManifest;
    }
}

}  // namespace

OtaOperationStatus OtaManager::operation_status_from_submit_status(OtaSubmitStatus status) noexcept {
    switch (status) {
        case OtaSubmitStatus::kAccepted:
            return OtaOperationStatus::kOk;
        case OtaSubmitStatus::kBusy:
            return OtaOperationStatus::kNoCapacity;
        case OtaSubmitStatus::kInvalidRequest:
            return OtaOperationStatus::kInvalidArgument;
        case OtaSubmitStatus::kInvalidManifest:
            return OtaOperationStatus::kManifestInvalid;
        case OtaSubmitStatus::kProjectMismatch:
            return OtaOperationStatus::kProjectMismatch;
        case OtaSubmitStatus::kBoardMismatch:
            return OtaOperationStatus::kBoardMismatch;
        case OtaSubmitStatus::kChipTargetMismatch:
            return OtaOperationStatus::kChipTargetMismatch;
        case OtaSubmitStatus::kSchemaMismatch:
            return OtaOperationStatus::kSchemaMismatch;
        case OtaSubmitStatus::kDowngradeRejected:
            return OtaOperationStatus::kDowngradeRejected;
        default:
            return OtaOperationStatus::kInternalError;
    }
}

OtaSubmitStatus OtaManager::enqueue_request(ServiceRuntime& runtime, const OtaStartRequest& request) noexcept {
    if (request.request_id == 0U || request.manifest.url[0] == '\0') {
        OTA_LOGW("OTA enqueue rejected invalid request id=%" PRIu32, request.request_id);
        return OtaSubmitStatus::kInvalidRequest;
    }

    OtaStartRequest normalized_request = request;
    const OtaManifestContext manifest_context = build_manifest_context(runtime);
    apply_manifest_defaults_to_request(manifest_context, &normalized_request);
    const OtaSubmitStatus manifest_status =
        map_manifest_status(validate_ota_manifest(normalized_request.manifest, manifest_context));
    if (manifest_status != OtaSubmitStatus::kAccepted) {
        OTA_LOGW(
            "OTA enqueue rejected manifest request_id=%" PRIu32 " status=%u",
            normalized_request.request_id,
            static_cast<unsigned>(manifest_status));
        return manifest_status;
    }

    {
        RuntimeLockGuard guard(queue_lock_);
        if (request_slot_.in_use || busy_.load(std::memory_order_acquire)) {
            OTA_LOGW(
                "OTA enqueue busy request_id=%" PRIu32 " in_use=%d busy=%d",
                normalized_request.request_id,
                request_slot_.in_use ? 1 : 0,
                busy_.load(std::memory_order_relaxed) ? 1 : 0);
            return OtaSubmitStatus::kBusy;
        }

        request_slot_.request = normalized_request;
        request_slot_.in_use = true;
        pending_count_.store(1U, std::memory_order_release);
    }

    OTA_LOGI(
        "OTA enqueue accepted request_id=%" PRIu32 " version=%s url=%s",
        normalized_request.request_id,
        normalized_request.manifest.version[0] != '\0' ? normalized_request.manifest.version.data() : "<auto>",
        normalized_request.manifest.url.data());
    runtime.note_ota_poll_status(normalized_request.request_id, OtaPollStatus::kQueued);
    publish_status_queued(normalized_request);
    return OtaSubmitStatus::kAccepted;
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
    OTA_LOGI("OTA worker popped request_id=%" PRIu32, out->request_id);
    return true;
}

void OtaManager::publish_status_queued(const OtaStartRequest& request) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kQueued;
    pending_status_update_.request_id = request.request_id;
    pending_status_update_.target_version = manifest_version_or_empty(request.manifest);
}

void OtaManager::publish_status_started(const OtaStartRequest& request) noexcept {
    RuntimeLockGuard guard(update_lock_);
    pending_status_update_ = StatusUpdate{};
    pending_status_update_.kind = StatusUpdateKind::kStarted;
    pending_status_update_.request_id = request.request_id;
    pending_status_update_.target_version = manifest_version_or_empty(request.manifest);
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
    OTA_LOGI("OTA worker starting request_id=%" PRIu32, request.request_id);
    publish_status_started(request);
    runtime.note_ota_poll_status(request.request_id, OtaPollStatus::kDownloading);

    hal_ota_https_request_t hal_request{};
    hal_request.url = request.manifest.url.data();
    hal_request.expected_version = request.manifest.version[0] != '\0' ? request.manifest.version.data() : nullptr;
    hal_request.expected_project_name =
        request.manifest.project[0] != '\0' ? request.manifest.project.data() : nullptr;

    hal_ota_https_result_t hal_result{};
    ProgressContext progress_context{};
    progress_context.manager = this;
    progress_context.request_id = request.request_id;

#ifdef ESP_PLATFORM
    bool mqtt_paused_for_ota = false;
    if (hal_mqtt_is_enabled()) {
        const hal_mqtt_status_t mqtt_stop_status = hal_mqtt_stop();
        OTA_LOGI(
            "OTA worker mqtt stop request_id=%" PRIu32 " status=%d",
            request.request_id,
            static_cast<int>(mqtt_stop_status));
        if (mqtt_stop_status == HAL_MQTT_STATUS_OK) {
            mqtt_paused_for_ota = true;
            vTaskDelay(kOtaWorkerPreflightDelayTicks);
        }
    }
#endif

    OTA_LOGI("OTA worker invoking hal_ota request_id=%" PRIu32, request.request_id);
    const int hal_rc = hal_ota_perform_https_update(
        &hal_request,
        &OtaManager::progress_callback,
        &progress_context,
        &hal_result);
    OTA_LOGI(
        "OTA worker hal_ota finished request_id=%" PRIu32 " hal_rc=%d hal_status=%d bytes=%" PRIu32 " size=%" PRIu32,
        request.request_id,
        hal_rc,
        static_cast<int>(hal_result.status),
        hal_result.bytes_read,
        hal_result.image_size);

    OtaResult result{};
    result.request_id = request.request_id;
    result.status = map_hal_status(hal_result.status);
    result.reboot_required = hal_result.reboot_required;
    result.downloaded_bytes = hal_result.bytes_read;
    result.image_size = hal_result.image_size;
    result.image_size_known = hal_result.image_size_known;
    result.target_version = manifest_version_or_empty(request.manifest);

    if (result.target_version[0] == '\0' && hal_result.discovered_version[0] != '\0') {
        (void)copy_chars(hal_result.discovered_version, result.target_version);
    }

    if (hal_rc != 0 && result.status == OtaOperationStatus::kOk) {
        result.status = OtaOperationStatus::kInternalError;
    }

#ifdef ESP_PLATFORM
    if (mqtt_paused_for_ota && result.status != OtaOperationStatus::kOk) {
        if (kOtaWorkerMqttResumeDelayTicks > 0) {
            OTA_LOGI(
                "OTA worker delaying mqtt resume request_id=%" PRIu32 " delay_ms=%u",
                request.request_id,
                static_cast<unsigned>(CONFIG_ZGW_MQTT_RESUME_AFTER_OTA_DELAY_MS));
            vTaskDelay(kOtaWorkerMqttResumeDelayTicks);
        }
        const hal_mqtt_status_t mqtt_start_status = hal_mqtt_start();
        OTA_LOGI(
            "OTA worker mqtt resume request_id=%" PRIu32 " status=%d",
            request.request_id,
            static_cast<int>(mqtt_start_status));
    }
#endif

    publish_status_finished(result);
    busy_.store(false, std::memory_order_release);
    active_request_id_.store(0U, std::memory_order_release);

    const bool queued = runtime.queue_ota_result(result);
    OTA_LOGI(
        "OTA worker completed request_id=%" PRIu32 " result_status=%u queued=%d reboot_required=%d",
        request.request_id,
        static_cast<unsigned>(result.status),
        queued ? 1 : 0,
        result.reboot_required ? 1 : 0);

#ifdef ESP_PLATFORM
#if CONFIG_ZGW_OTA_AUTO_REBOOT_ENABLED
    if (result.status == OtaOperationStatus::kOk && result.reboot_required) {
        publish_status_reboot_pending(result.request_id);
        OTA_LOGI("OTA worker scheduling reboot request_id=%" PRIu32, result.request_id);
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

    OTA_LOGI("OTA worker task started");
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
