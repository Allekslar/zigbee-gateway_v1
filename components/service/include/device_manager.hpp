/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "core_events.hpp"
#include "core_state.hpp"

namespace service {

struct DeviceRuntimeSnapshot {
    bool join_window_open{false};
    uint16_t join_window_seconds_left{0};
    std::array<uint32_t, core::kMaxDevices> force_remove_ms_left{};
    std::array<core::CoreReportingState, core::kMaxDevices> reporting_state{};
    std::array<uint32_t, core::kMaxDevices> last_report_at_ms{};
    std::array<bool, core::kMaxDevices> stale{};
    std::array<uint8_t, core::kMaxDevices> battery_percent{};
    std::array<bool, core::kMaxDevices> has_battery{};
    std::array<uint8_t, core::kMaxDevices> lqi{};
    std::array<bool, core::kMaxDevices> has_lqi{};
};

class DeviceManager {
public:
    static constexpr std::size_t kMaxPendingForceRemove = 16;
    static constexpr std::size_t kJoinCandidateHistory = 8;
    static constexpr uint32_t kJoinDedupWindowMs = 5000U;

    bool handle_event(const core::CoreEvent& event) noexcept;
    bool is_duplicate_join_candidate(uint16_t short_addr, uint32_t now_ms) noexcept;
    bool schedule_force_remove(uint16_t short_addr, uint32_t deadline_ms) noexcept;
    bool get_force_remove_remaining_ms(uint16_t short_addr, uint32_t now_ms, uint32_t* remaining_ms) const noexcept;
    bool build_runtime_snapshot(
        const core::CoreState& state,
        uint32_t now_ms,
        bool join_window_open,
        uint16_t join_window_seconds_left,
        DeviceRuntimeSnapshot* out) const noexcept;
    std::size_t collect_expired_force_remove(
        uint32_t now_ms,
        std::array<uint16_t, kMaxPendingForceRemove>* expired_short_addrs) noexcept;

private:
    struct PendingForceRemove {
        bool in_use{false};
        uint16_t short_addr{core::kUnknownDeviceShortAddr};
        uint32_t deadline_ms{0};
    };

    struct JoinCandidateEntry {
        uint16_t short_addr{core::kUnknownDeviceShortAddr};
        uint32_t seen_at_ms{0};
    };

    void clear_join_candidate(uint16_t short_addr) noexcept;
    static bool is_deadline_reached(uint32_t now_ms, uint32_t deadline_ms) noexcept;

    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<PendingForceRemove, kMaxPendingForceRemove> pending_force_remove_{};
    std::array<JoinCandidateEntry, kJoinCandidateHistory> join_candidate_history_{};
};

}  // namespace service
