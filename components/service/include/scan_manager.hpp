/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "runtime_lock.hpp"

namespace service {

class ServiceRuntime;

class ScanManager {
public:
    static constexpr std::size_t kQueueCapacity = 8;

    bool enqueue_request(uint32_t request_id) noexcept;
    bool pop_request_for_test(uint32_t* request_id) noexcept;
    bool is_request_queued(uint32_t request_id) const noexcept;
    bool is_request_in_progress(uint32_t request_id) const noexcept;
    uint32_t active_request_id() const noexcept;
    void set_request_in_progress_for_test(uint32_t request_id) noexcept;
    void clear_request_in_progress_for_test() noexcept;
    std::size_t pending_ingress_count() const noexcept;
    bool has_pending_or_busy() const noexcept;

#ifdef ESP_PLATFORM
    static void worker_task_entry(void* arg);
    void run_worker_loop(ServiceRuntime& runtime) noexcept;
#endif

private:
    struct Request {
        uint32_t request_id{0};
    };

    bool pop_request(Request* out) noexcept;

    mutable RuntimeLock queue_lock_{};
    std::array<Request, kQueueCapacity> queue_{};
    std::size_t queue_head_{0};
    std::size_t queue_tail_{0};
    std::size_t queue_count_{0};

    std::atomic<bool> busy_{false};
    std::atomic<uint32_t> pending_count_{0};
    std::atomic<uint32_t> active_request_id_{0};
};

}  // namespace service
