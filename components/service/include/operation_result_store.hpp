/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "network_manager.hpp"
#include "runtime_lock.hpp"
#include "service_runtime_api.hpp"

namespace service {

class OperationResultStore {
public:
    static constexpr std::size_t kNetworkResultQueueCapacity = 16;
    static constexpr std::size_t kConfigResultQueueCapacity = 8;

    uint32_t next_request_id() noexcept;
    void note_network_poll_status(uint32_t request_id, NetworkOperationPollStatus status) noexcept;
    bool publish_config_result(const ConfigResult& result) noexcept;
    bool take_config_result(uint32_t request_id, ConfigResult* out) noexcept;
    bool publish_network_result(const NetworkResult& result) noexcept;
    bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept;
    NetworkOperationPollStatus get_network_operation_poll_status(uint32_t request_id) const noexcept;
    std::size_t pending_config_results() const noexcept;
    std::size_t pending_network_results() const noexcept;

private:
    struct PollStatusEntry {
        uint32_t request_id{0};
        NetworkOperationPollStatus status{NetworkOperationPollStatus::kNotReady};
    };

    bool upsert_poll_status_locked(uint32_t request_id, NetworkOperationPollStatus status) noexcept;
    void remove_poll_status_locked(uint32_t request_id) noexcept;

    std::atomic<uint32_t> next_request_id_{1U};
    mutable RuntimeLock network_result_lock_{};
    std::array<ConfigResult, kConfigResultQueueCapacity> config_result_queue_{};
    std::size_t config_result_count_{0};
    std::array<NetworkResult, kNetworkResultQueueCapacity> network_result_queue_{};
    std::size_t network_result_count_{0};
    std::array<PollStatusEntry, kNetworkResultQueueCapacity> poll_status_queue_{};
    std::size_t poll_status_count_{0};
};

}  // namespace service
