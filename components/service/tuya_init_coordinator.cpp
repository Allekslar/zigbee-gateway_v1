/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_init_coordinator.hpp"

namespace service {

void TuyaInitCoordinator::notify_device_resolved(
    uint16_t short_addr,
    const TuyaInitPlan& plan) noexcept {
    TuyaInitEntry* entry = find(short_addr);
    if (entry == nullptr) {
        entry = allocate(short_addr);
    }
    if (entry == nullptr) {
        return;
    }
    entry->plan = plan;
    entry->current_step = 0;
    entry->retries_left = kTuyaInitMaxRetries;
    entry->correlation_id = 0;
    entry->deadline_ms = 0;
    if (plan.empty()) {
        entry->status = TuyaInitStatus::kReady;
    } else {
        entry->status = TuyaInitStatus::kInitPending;
    }
}

void TuyaInitCoordinator::notify_device_removed(uint16_t short_addr) noexcept {
    TuyaInitEntry* entry = find(short_addr);
    if (entry != nullptr) {
        *entry = TuyaInitEntry{};
    }
}

TuyaInitAction TuyaInitCoordinator::tick(uint32_t now_ms) noexcept {
    for (auto& entry : entries_) {
        if (!entry.in_use || entry.status != TuyaInitStatus::kWaitingAck) {
            continue;
        }
        if (now_ms >= entry.deadline_ms) {
            if (entry.retries_left > 0U) {
                --entry.retries_left;
                entry.status = TuyaInitStatus::kInitPending;
            } else {
                entry.status = TuyaInitStatus::kDegraded;
            }
        }
    }

    for (auto& entry : entries_) {
        if (!entry.in_use || entry.status != TuyaInitStatus::kInitPending) {
            continue;
        }
        if (entry.current_step >= entry.plan.step_count) {
            entry.status = TuyaInitStatus::kReady;
            continue;
        }
        const TuyaInitStep& step = entry.plan.steps[entry.current_step];
        TuyaInitAction action{};
        action.pending = true;
        action.short_addr = entry.short_addr;
        action.endpoint = step.endpoint;
        action.dp_id = step.dp_id;
        action.dp_type = step.dp_type;
        action.value_len = step.value_len;
        for (uint8_t i = 0; i < step.value_len && i < kTuyaDpCommandValueMaxLen; ++i) {
            action.value[i] = step.value[i];
        }
        const uint32_t corr_id = next_correlation_id();
        action.correlation_id = corr_id;
        entry.correlation_id = corr_id;
        entry.deadline_ms = now_ms + kTuyaInitStepTimeoutMs;
        entry.status = TuyaInitStatus::kWaitingAck;
        return action;
    }

    return TuyaInitAction{};
}

bool TuyaInitCoordinator::notify_ack(uint32_t correlation_id, bool success) noexcept {
    TuyaInitEntry* entry = find_by_correlation_id(correlation_id);
    if (entry == nullptr) {
        return false;
    }
    if (entry->status != TuyaInitStatus::kWaitingAck) {
        return false;
    }
    if (success) {
        ++entry->current_step;
        entry->retries_left = kTuyaInitMaxRetries;
        if (entry->current_step >= entry->plan.step_count) {
            entry->status = TuyaInitStatus::kReady;
        } else {
            entry->status = TuyaInitStatus::kInitPending;
        }
    } else {
        if (entry->retries_left > 0U) {
            --entry->retries_left;
            entry->status = TuyaInitStatus::kInitPending;
        } else {
            entry->status = TuyaInitStatus::kDegraded;
        }
    }
    return true;
}

TuyaInitStatus TuyaInitCoordinator::status(uint16_t short_addr) const noexcept {
    const TuyaInitEntry* entry = find(short_addr);
    if (entry == nullptr) {
        return TuyaInitStatus::kNotStarted;
    }
    return entry->status;
}

void TuyaInitCoordinator::clear() noexcept {
    for (auto& entry : entries_) {
        entry = TuyaInitEntry{};
    }
    correlation_id_counter_ = kTuyaInitCorrelationIdBase;
}

TuyaInitEntry* TuyaInitCoordinator::find(uint16_t short_addr) noexcept {
    for (auto& entry : entries_) {
        if (entry.in_use && entry.short_addr == short_addr) {
            return &entry;
        }
    }
    return nullptr;
}

const TuyaInitEntry* TuyaInitCoordinator::find(uint16_t short_addr) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.in_use && entry.short_addr == short_addr) {
            return &entry;
        }
    }
    return nullptr;
}

TuyaInitEntry* TuyaInitCoordinator::allocate(uint16_t short_addr) noexcept {
    for (auto& entry : entries_) {
        if (!entry.in_use) {
            entry.in_use = true;
            entry.short_addr = short_addr;
            return &entry;
        }
    }
    return nullptr;
}

TuyaInitEntry* TuyaInitCoordinator::find_by_correlation_id(uint32_t correlation_id) noexcept {
    for (auto& entry : entries_) {
        if (entry.in_use && entry.correlation_id == correlation_id) {
            return &entry;
        }
    }
    return nullptr;
}

uint32_t TuyaInitCoordinator::next_correlation_id() noexcept {
    return correlation_id_counter_++;
}

}  // namespace service
