/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "network_manager.hpp"
#include "runtime_lock.hpp"

namespace service {

class OperationResultStore {
public:
    static constexpr std::size_t kNetworkResultQueueCapacity = 16;

    uint32_t next_request_id() noexcept;
    bool publish_network_result(const NetworkResult& result) noexcept;
    bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept;
    std::size_t pending_network_results() const noexcept;

private:
    std::atomic<uint32_t> next_request_id_{1U};
    mutable RuntimeLock network_result_lock_{};
    std::array<NetworkResult, kNetworkResultQueueCapacity> network_result_queue_{};
    std::size_t network_result_count_{0};
};

}  // namespace service
