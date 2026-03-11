/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "operation_result_store.hpp"

namespace service {

bool OperationResultStore::upsert_poll_status_locked(
    uint32_t request_id,
    NetworkOperationPollStatus status) noexcept {
    if (request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < poll_status_count_; ++i) {
        if (poll_status_queue_[i].request_id == request_id) {
            poll_status_queue_[i].status = status;
            return true;
        }
    }

    if (poll_status_count_ >= poll_status_queue_.size()) {
        for (std::size_t i = 1; i < poll_status_count_; ++i) {
            poll_status_queue_[i - 1U] = poll_status_queue_[i];
        }
        --poll_status_count_;
    }

    poll_status_queue_[poll_status_count_].request_id = request_id;
    poll_status_queue_[poll_status_count_].status = status;
    ++poll_status_count_;
    return true;
}

void OperationResultStore::remove_poll_status_locked(uint32_t request_id) noexcept {
    for (std::size_t i = 0; i < poll_status_count_; ++i) {
        if (poll_status_queue_[i].request_id != request_id) {
            continue;
        }
        for (std::size_t j = i + 1U; j < poll_status_count_; ++j) {
            poll_status_queue_[j - 1U] = poll_status_queue_[j];
        }
        --poll_status_count_;
        return;
    }
}

uint32_t OperationResultStore::next_request_id() noexcept {
    for (;;) {
        uint32_t current = next_request_id_.load(std::memory_order_acquire);
        uint32_t next = current + 1U;
        if (next == 0U) {
            next = 1U;
        }
        if (next_request_id_.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return current;
        }
    }
}

void OperationResultStore::note_network_poll_status(
    uint32_t request_id,
    NetworkOperationPollStatus status) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (status == NetworkOperationPollStatus::kNotReady) {
        remove_poll_status_locked(request_id);
        return;
    }
    (void)upsert_poll_status_locked(request_id, status);
}

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
    (void)upsert_poll_status_locked(result.request_id, NetworkOperationPollStatus::kReady);
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
        remove_poll_status_locked(request_id);
        return true;
    }

    return false;
}

NetworkOperationPollStatus OperationResultStore::get_network_operation_poll_status(uint32_t request_id) const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (request_id == 0U) {
        return NetworkOperationPollStatus::kNotReady;
    }

    for (std::size_t i = 0; i < network_result_count_; ++i) {
        if (network_result_queue_[i].request_id == request_id) {
            return NetworkOperationPollStatus::kReady;
        }
    }

    for (std::size_t i = 0; i < poll_status_count_; ++i) {
        if (poll_status_queue_[i].request_id == request_id) {
            return poll_status_queue_[i].status;
        }
    }

    return NetworkOperationPollStatus::kNotReady;
}

std::size_t OperationResultStore::pending_network_results() const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    return network_result_count_;
}

}  // namespace service
