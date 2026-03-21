/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstddef>

#include "runtime_lock.hpp"
#include "service_runtime_api.hpp"

namespace service {

class ServiceRuntime;

class RcpUpdateManager {
public:
    RcpUpdateSubmitStatus enqueue_request(ServiceRuntime& runtime, const RcpUpdateRequest& request) noexcept;
    bool build_api_snapshot(RcpUpdateApiSnapshot* out) const noexcept;
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
        RcpUpdateRequest request{};
    };

    enum class StatusUpdateKind : uint8_t {
        kNone = 0,
        kQueued = 1,
        kStarted = 2,
        kFinished = 3,
    };

    struct StatusUpdate {
        StatusUpdateKind kind{StatusUpdateKind::kNone};
        uint32_t request_id{0};
        RcpUpdateOperationStatus status{RcpUpdateOperationStatus::kOk};
        uint32_t written_bytes{0};
        std::array<char, RcpUpdateResult::kVersionMaxLen> target_version{};
    };

    bool pop_request(RcpUpdateRequest* out) noexcept;
    bool process_request(ServiceRuntime& runtime, const RcpUpdateRequest& request) noexcept;
    void publish_status_queued(const RcpUpdateRequest& request) noexcept;
    void publish_status_started(const RcpUpdateRequest& request) noexcept;
    void publish_status_finished(const RcpUpdateResult& result) noexcept;
    bool take_status_update(StatusUpdate* out) noexcept;
    bool apply_status_update(const StatusUpdate& update) noexcept;

    mutable RuntimeLock queue_lock_{};
    mutable RuntimeLock update_lock_{};
    mutable RuntimeLock snapshot_lock_{};
    RequestSlot request_slot_{};
    StatusUpdate pending_status_update_{};
    RcpUpdateApiSnapshot snapshot_{};
    std::atomic<bool> busy_{false};
    std::atomic<uint32_t> pending_count_{0};

    friend class ServiceRuntime;
};

}  // namespace service
