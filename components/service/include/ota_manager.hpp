/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstddef>

#include "runtime_lock.hpp"
#include "service_runtime_api.hpp"

namespace service {

class ServiceRuntime;

class OtaManager {
public:
    OtaSubmitStatus enqueue_request(ServiceRuntime& runtime, const OtaStartRequest& request) noexcept;
    bool build_api_snapshot(OtaApiSnapshot* out) const noexcept;
    std::size_t pending_ingress_count() const noexcept;
    bool has_pending_or_busy() const noexcept;

#ifdef ESP_PLATFORM
    static void worker_task_entry(void* arg);
    void run_worker_loop(ServiceRuntime& runtime) noexcept;
#else
    bool drain_requests(ServiceRuntime& runtime) noexcept;
#endif

private:
    struct RequestSlot {
        bool in_use{false};
        OtaStartRequest request{};
    };

    enum class StatusUpdateKind : uint8_t {
        kNone = 0,
        kQueued = 1,
        kStarted = 2,
        kProgress = 3,
        kFinished = 4,
        kRebootPending = 5,
    };

    struct StatusUpdate {
        StatusUpdateKind kind{StatusUpdateKind::kNone};
        uint32_t request_id{0};
        OtaOperationStatus status{OtaOperationStatus::kOk};
        uint32_t downloaded_bytes{0};
        uint32_t image_size{0};
        bool image_size_known{false};
        uint32_t transport_last_esp_err{0};
        uint32_t transport_last_tls_error{0};
        int32_t transport_tls_error_code{0};
        int32_t transport_tls_flags{0};
        int32_t transport_socket_errno{0};
        int32_t transport_http_status_code{0};
        uint8_t transport_failure_stage{0};
        std::array<char, OtaResult::kVersionMaxLen> target_version{};
    };

    struct ProgressContext {
        OtaManager* manager{nullptr};
        uint32_t request_id{0};
    };

    static OtaOperationStatus operation_status_from_submit_status(OtaSubmitStatus status) noexcept;
    bool pop_request(OtaStartRequest* out) noexcept;
    bool process_request(ServiceRuntime& runtime, const OtaStartRequest& request) noexcept;
    void publish_status_queued(const OtaStartRequest& request) noexcept;
    void publish_status_started(const OtaStartRequest& request) noexcept;
    void publish_status_progress(
        uint32_t request_id,
        uint32_t bytes_read,
        uint32_t image_size,
        bool image_size_known) noexcept;
    void publish_status_finished(const OtaResult& result) noexcept;
    void publish_status_reboot_pending(uint32_t request_id) noexcept;
    bool take_status_update(StatusUpdate* out) noexcept;
    bool apply_status_update(const StatusUpdate& update) noexcept;
    static void progress_callback(uint32_t bytes_read, uint32_t image_size, bool image_size_known, void* user_ctx) noexcept;

    mutable RuntimeLock queue_lock_{};
    mutable RuntimeLock update_lock_{};
    mutable RuntimeLock snapshot_lock_{};
    RequestSlot request_slot_{};
    StatusUpdate pending_status_update_{};
    OtaApiSnapshot snapshot_{};
    std::atomic<bool> busy_{false};
    std::atomic<uint32_t> pending_count_{0};
    std::atomic<uint32_t> active_request_id_{0};

    friend class ServiceRuntime;
};

}  // namespace service
