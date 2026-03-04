/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "device_manager.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace service {

namespace {

class SpinLockGuard {
public:
    explicit SpinLockGuard(std::atomic_flag& lock) noexcept : lock_(lock) {
        while (lock_.test_and_set(std::memory_order_acquire)) {
#ifdef ESP_PLATFORM
            // Avoid priority inversion: let lower-priority lock owner run.
            vTaskDelay(1);
#endif
        }
    }

    ~SpinLockGuard() noexcept {
        lock_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag& lock_;
};

}  // namespace

bool DeviceManager::is_deadline_reached(uint32_t now_ms, uint32_t deadline_ms) noexcept {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool DeviceManager::handle_event(const core::CoreEvent& event) noexcept {
    if (event.type != core::CoreEventType::kDeviceLeft ||
        event.device_short_addr == core::kUnknownDeviceShortAddr ||
        event.device_short_addr == 0x0000U) {
        return false;
    }

    SpinLockGuard guard(lock_);
    bool changed = false;
    for (std::size_t i = 0; i < pending_force_remove_.size(); ++i) {
        PendingForceRemove& pending = pending_force_remove_[i];
        if (pending.in_use && pending.short_addr == event.device_short_addr) {
            pending = PendingForceRemove{};
            changed = true;
        }
    }

    clear_join_candidate(event.device_short_addr);
    return changed;
}

bool DeviceManager::is_duplicate_join_candidate(uint16_t short_addr, uint32_t now_ms) noexcept {
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return true;
    }

    SpinLockGuard guard(lock_);

    JoinCandidateEntry* free_slot = nullptr;
    JoinCandidateEntry* oldest = &join_candidate_history_[0];

    for (std::size_t i = 0; i < join_candidate_history_.size(); ++i) {
        JoinCandidateEntry* entry = &join_candidate_history_[i];
        if (entry->short_addr == short_addr) {
            const uint32_t delta_ms = now_ms - entry->seen_at_ms;
            if (delta_ms <= kJoinDedupWindowMs) {
                return true;
            }
            entry->seen_at_ms = now_ms;
            return false;
        }

        if (entry->short_addr == core::kUnknownDeviceShortAddr) {
            if (free_slot == nullptr) {
                free_slot = entry;
            }
            continue;
        }

        if (entry->seen_at_ms < oldest->seen_at_ms) {
            oldest = entry;
        }
    }

    JoinCandidateEntry* target = free_slot != nullptr ? free_slot : oldest;
    target->short_addr = short_addr;
    target->seen_at_ms = now_ms;
    return false;
}

bool DeviceManager::schedule_force_remove(uint16_t short_addr, uint32_t deadline_ms) noexcept {
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return false;
    }

    SpinLockGuard guard(lock_);
    PendingForceRemove* free_slot = nullptr;

    for (std::size_t i = 0; i < pending_force_remove_.size(); ++i) {
        PendingForceRemove& pending = pending_force_remove_[i];
        if (!pending.in_use) {
            if (free_slot == nullptr) {
                free_slot = &pending;
            }
            continue;
        }

        if (pending.short_addr == short_addr) {
            pending.deadline_ms = deadline_ms;
            return true;
        }
    }

    if (free_slot == nullptr) {
        return false;
    }

    free_slot->in_use = true;
    free_slot->short_addr = short_addr;
    free_slot->deadline_ms = deadline_ms;
    return true;
}

bool DeviceManager::get_force_remove_remaining_ms(
    uint16_t short_addr,
    uint32_t now_ms,
    uint32_t* remaining_ms) const noexcept {
    if (remaining_ms == nullptr || short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return false;
    }

    *remaining_ms = 0U;
    SpinLockGuard guard(lock_);
    for (std::size_t i = 0; i < pending_force_remove_.size(); ++i) {
        const PendingForceRemove& pending = pending_force_remove_[i];
        if (!pending.in_use || pending.short_addr != short_addr) {
            continue;
        }

        if (is_deadline_reached(now_ms, pending.deadline_ms)) {
            *remaining_ms = 0U;
        } else {
            *remaining_ms = pending.deadline_ms - now_ms;
        }
        return true;
    }

    return false;
}

bool DeviceManager::build_runtime_snapshot(
    const core::CoreState& state,
    uint32_t now_ms,
    bool join_window_open,
    uint16_t join_window_seconds_left,
    DeviceRuntimeSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    *out = DeviceRuntimeSnapshot{};
    out->join_window_open = join_window_open;
    out->join_window_seconds_left = join_window_open ? join_window_seconds_left : 0U;

    SpinLockGuard guard(lock_);
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        const uint16_t short_addr = state.devices[i].short_addr;
        if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U || !state.devices[i].online) {
            continue;
        }

        out->reporting_state[i] = state.devices[i].reporting_state;
        out->last_report_at_ms[i] = state.devices[i].last_report_at_ms;
        out->stale[i] = state.devices[i].stale;
        out->battery_percent[i] = state.devices[i].battery_percent;
        out->has_battery[i] = state.devices[i].has_battery;
        out->lqi[i] = state.devices[i].lqi;
        out->has_lqi[i] = state.devices[i].has_lqi;

        uint32_t remaining_ms = 0U;
        for (std::size_t j = 0; j < pending_force_remove_.size(); ++j) {
            const PendingForceRemove& pending = pending_force_remove_[j];
            if (!pending.in_use || pending.short_addr != short_addr) {
                continue;
            }

            if (!is_deadline_reached(now_ms, pending.deadline_ms)) {
                remaining_ms = pending.deadline_ms - now_ms;
            }
            break;
        }
        out->force_remove_ms_left[i] = remaining_ms;
    }

    return true;
}

std::size_t DeviceManager::collect_expired_force_remove(
    uint32_t now_ms,
    std::array<uint16_t, kMaxPendingForceRemove>* expired_short_addrs) noexcept {
    if (expired_short_addrs == nullptr) {
        return 0;
    }

    std::size_t expired_count = 0;
    SpinLockGuard guard(lock_);
    for (std::size_t i = 0; i < pending_force_remove_.size(); ++i) {
        PendingForceRemove& pending = pending_force_remove_[i];
        if (!pending.in_use || !is_deadline_reached(now_ms, pending.deadline_ms)) {
            continue;
        }

        if (expired_count < expired_short_addrs->size()) {
            (*expired_short_addrs)[expired_count] = pending.short_addr;
            ++expired_count;
        }
        pending = PendingForceRemove{};
    }
    return expired_count;
}

void DeviceManager::clear_join_candidate(uint16_t short_addr) noexcept {
    if (short_addr == core::kUnknownDeviceShortAddr || short_addr == 0x0000U) {
        return;
    }

    for (std::size_t i = 0; i < join_candidate_history_.size(); ++i) {
        JoinCandidateEntry& entry = join_candidate_history_[i];
        if (entry.short_addr == short_addr) {
            entry = JoinCandidateEntry{};
        }
    }
}

}  // namespace service
