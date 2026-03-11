/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "operation_result_store.hpp"

namespace service {

bool OperationResultStore::publish_network_result(const NetworkResult& result) noexcept {
    RuntimeLockGuard guard(network_result_lock_);

    for (std::size_t i = 0; i < network_result_count_; ++i) {
        if (network_result_queue_[i].request_id == result.request_id && result.request_id != 0U) {
            network_result_queue_[i] = result;
            return true;
        }
    }

    if (network_result_count_ >= kNetworkResultQueueCapacity) {
        for (std::size_t i = 1; i < network_result_count_; ++i) {
            network_result_queue_[i - 1U] = network_result_queue_[i];
        }
        --network_result_count_;
    }

    network_result_queue_[network_result_count_] = result;
    ++network_result_count_;
    return true;
}

bool OperationResultStore::take_network_result(uint32_t request_id, NetworkResult* out) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (out == nullptr || request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < network_result_count_; ++i) {
        if (network_result_queue_[i].request_id != request_id) {
            continue;
        }

        *out = network_result_queue_[i];
        for (std::size_t j = i + 1U; j < network_result_count_; ++j) {
            network_result_queue_[j - 1U] = network_result_queue_[j];
        }
        --network_result_count_;
        return true;
    }

    return false;
}

std::size_t OperationResultStore::pending_network_results() const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    return network_result_count_;
}

}  // namespace service
