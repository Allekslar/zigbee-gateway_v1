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
    static constexpr std::size_t kOtaResultQueueCapacity = 4;

    uint32_t next_request_id() noexcept;
    void note_network_poll_status(uint32_t request_id, NetworkOperationPollStatus status) noexcept;
    void note_ota_poll_status(uint32_t request_id, OtaPollStatus status) noexcept;
    bool publish_config_result(const ConfigResult& result) noexcept;
    bool take_config_result(uint32_t request_id, ConfigResult* out) noexcept;
    bool publish_network_result(const NetworkResult& result) noexcept;
    bool take_network_result(uint32_t request_id, NetworkResult* out) noexcept;
    bool publish_ota_result(const OtaResult& result) noexcept;
    bool take_ota_result(uint32_t request_id, OtaResult* out) noexcept;
    NetworkOperationPollStatus get_network_operation_poll_status(uint32_t request_id) const noexcept;
    OtaPollStatus get_ota_poll_status(uint32_t request_id) const noexcept;
    std::size_t pending_config_results() const noexcept;
    std::size_t pending_network_results() const noexcept;
    std::size_t pending_ota_results() const noexcept;

private:
    struct NetworkPollStatusEntry {
        uint32_t request_id{0};
        NetworkOperationPollStatus status{NetworkOperationPollStatus::kNotReady};
    };

    struct OtaPollStatusEntry {
        uint32_t request_id{0};
        OtaPollStatus status{OtaPollStatus::kNotReady};
    };

    bool upsert_network_poll_status_locked(uint32_t request_id, NetworkOperationPollStatus status) noexcept;
    void remove_network_poll_status_locked(uint32_t request_id) noexcept;
    bool upsert_ota_poll_status_locked(uint32_t request_id, OtaPollStatus status) noexcept;
    void remove_ota_poll_status_locked(uint32_t request_id) noexcept;

    std::atomic<uint32_t> next_request_id_{1U};
    mutable RuntimeLock network_result_lock_{};
    std::array<ConfigResult, kConfigResultQueueCapacity> config_result_queue_{};
    std::size_t config_result_count_{0};
    std::array<NetworkResult, kNetworkResultQueueCapacity> network_result_queue_{};
    std::size_t network_result_count_{0};
    std::array<NetworkPollStatusEntry, kNetworkResultQueueCapacity> network_poll_status_queue_{};
    std::size_t network_poll_status_count_{0};
    std::array<OtaResult, kOtaResultQueueCapacity> ota_result_queue_{};
    std::size_t ota_result_count_{0};
    std::array<OtaPollStatusEntry, kOtaResultQueueCapacity> ota_poll_status_queue_{};
    std::size_t ota_poll_status_count_{0};
};

}  // namespace service
