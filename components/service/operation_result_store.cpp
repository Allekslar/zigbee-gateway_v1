/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "operation_result_store.hpp"

#include <limits>

namespace service {

bool OperationResultStore::upsert_network_poll_status_locked(
    uint32_t request_id,
    NetworkOperationPollStatus status) noexcept {
    if (request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < network_poll_status_count_; ++i) {
        if (network_poll_status_queue_[i].request_id == request_id) {
            network_poll_status_queue_[i].status = status;
            return true;
        }
    }

    if (network_poll_status_count_ >= network_poll_status_queue_.size()) {
        for (std::size_t i = 1; i < network_poll_status_count_; ++i) {
            network_poll_status_queue_[i - 1U] = network_poll_status_queue_[i];
        }
        --network_poll_status_count_;
    }

    network_poll_status_queue_[network_poll_status_count_].request_id = request_id;
    network_poll_status_queue_[network_poll_status_count_].status = status;
    ++network_poll_status_count_;
    return true;
}

void OperationResultStore::remove_network_poll_status_locked(uint32_t request_id) noexcept {
    for (std::size_t i = 0; i < network_poll_status_count_; ++i) {
        if (network_poll_status_queue_[i].request_id != request_id) {
            continue;
        }
        for (std::size_t j = i + 1U; j < network_poll_status_count_; ++j) {
            network_poll_status_queue_[j - 1U] = network_poll_status_queue_[j];
        }
        --network_poll_status_count_;
        return;
    }
}

bool OperationResultStore::upsert_ota_poll_status_locked(uint32_t request_id, OtaPollStatus status) noexcept {
    if (request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < ota_poll_status_count_; ++i) {
        if (ota_poll_status_queue_[i].request_id == request_id) {
            ota_poll_status_queue_[i].status = status;
            return true;
        }
    }

    if (ota_poll_status_count_ >= ota_poll_status_queue_.size()) {
        for (std::size_t i = 1; i < ota_poll_status_count_; ++i) {
            ota_poll_status_queue_[i - 1U] = ota_poll_status_queue_[i];
        }
        --ota_poll_status_count_;
    }

    ota_poll_status_queue_[ota_poll_status_count_].request_id = request_id;
    ota_poll_status_queue_[ota_poll_status_count_].status = status;
    ++ota_poll_status_count_;
    return true;
}

void OperationResultStore::remove_ota_poll_status_locked(uint32_t request_id) noexcept {
    for (std::size_t i = 0; i < ota_poll_status_count_; ++i) {
        if (ota_poll_status_queue_[i].request_id != request_id) {
            continue;
        }
        for (std::size_t j = i + 1U; j < ota_poll_status_count_; ++j) {
            ota_poll_status_queue_[j - 1U] = ota_poll_status_queue_[j];
        }
        --ota_poll_status_count_;
        return;
    }
}

uint32_t OperationResultStore::next_request_id() noexcept {
    for (;;) {
        uint32_t current = next_request_id_.load(std::memory_order_acquire);
        const uint32_t next =
            (current == std::numeric_limits<uint32_t>::max()) ? 1U : (current + 1U);
        if (next_request_id_.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return current;
        }
    }
}

bool OperationResultStore::publish_config_result(const ConfigResult& result) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (result.request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < config_result_count_; ++i) {
        if (config_result_queue_[i].request_id == result.request_id) {
            config_result_queue_[i] = result;
            return true;
        }
    }

    if (config_result_count_ >= kConfigResultQueueCapacity) {
        for (std::size_t i = 1; i < config_result_count_; ++i) {
            config_result_queue_[i - 1U] = config_result_queue_[i];
        }
        --config_result_count_;
    }

    config_result_queue_[config_result_count_] = result;
    ++config_result_count_;
    return true;
}

void OperationResultStore::note_network_poll_status(
    uint32_t request_id,
    NetworkOperationPollStatus status) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (status == NetworkOperationPollStatus::kNotReady) {
        remove_network_poll_status_locked(request_id);
        return;
    }
    (void)upsert_network_poll_status_locked(request_id, status);
}

void OperationResultStore::note_ota_poll_status(uint32_t request_id, OtaPollStatus status) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (status == OtaPollStatus::kNotReady) {
        remove_ota_poll_status_locked(request_id);
        return;
    }
    (void)upsert_ota_poll_status_locked(request_id, status);
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
    (void)upsert_network_poll_status_locked(result.request_id, NetworkOperationPollStatus::kReady);
    return true;
}

bool OperationResultStore::publish_ota_result(const OtaResult& result) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (result.request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < ota_result_count_; ++i) {
        if (ota_result_queue_[i].request_id == result.request_id) {
            ota_result_queue_[i] = result;
            (void)upsert_ota_poll_status_locked(result.request_id, OtaPollStatus::kReady);
            return true;
        }
    }

    if (ota_result_count_ >= kOtaResultQueueCapacity) {
        for (std::size_t i = 1; i < ota_result_count_; ++i) {
            ota_result_queue_[i - 1U] = ota_result_queue_[i];
        }
        --ota_result_count_;
    }

    ota_result_queue_[ota_result_count_] = result;
    ++ota_result_count_;
    (void)upsert_ota_poll_status_locked(result.request_id, OtaPollStatus::kReady);
    return true;
}

bool OperationResultStore::take_config_result(uint32_t request_id, ConfigResult* out) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (out == nullptr || request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < config_result_count_; ++i) {
        if (config_result_queue_[i].request_id != request_id) {
            continue;
        }

        *out = config_result_queue_[i];
        for (std::size_t j = i + 1U; j < config_result_count_; ++j) {
            config_result_queue_[j - 1U] = config_result_queue_[j];
        }
        --config_result_count_;
        return true;
    }

    return false;
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
        remove_network_poll_status_locked(request_id);
        return true;
    }

    return false;
}

bool OperationResultStore::take_ota_result(uint32_t request_id, OtaResult* out) noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (out == nullptr || request_id == 0U) {
        return false;
    }

    for (std::size_t i = 0; i < ota_result_count_; ++i) {
        if (ota_result_queue_[i].request_id != request_id) {
            continue;
        }

        *out = ota_result_queue_[i];
        for (std::size_t j = i + 1U; j < ota_result_count_; ++j) {
            ota_result_queue_[j - 1U] = ota_result_queue_[j];
        }
        --ota_result_count_;
        remove_ota_poll_status_locked(request_id);
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

    for (std::size_t i = 0; i < network_poll_status_count_; ++i) {
        if (network_poll_status_queue_[i].request_id == request_id) {
            return network_poll_status_queue_[i].status;
        }
    }

    return NetworkOperationPollStatus::kNotReady;
}

OtaPollStatus OperationResultStore::get_ota_poll_status(uint32_t request_id) const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    if (request_id == 0U) {
        return OtaPollStatus::kNotReady;
    }

    for (std::size_t i = 0; i < ota_result_count_; ++i) {
        if (ota_result_queue_[i].request_id == request_id) {
            return OtaPollStatus::kReady;
        }
    }

    for (std::size_t i = 0; i < ota_poll_status_count_; ++i) {
        if (ota_poll_status_queue_[i].request_id == request_id) {
            return ota_poll_status_queue_[i].status;
        }
    }

    return OtaPollStatus::kNotReady;
}

std::size_t OperationResultStore::pending_config_results() const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    return config_result_count_;
}

std::size_t OperationResultStore::pending_network_results() const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    return network_result_count_;
}

std::size_t OperationResultStore::pending_ota_results() const noexcept {
    RuntimeLockGuard guard(network_result_lock_);
    return ota_result_count_;
}

}  // namespace service
