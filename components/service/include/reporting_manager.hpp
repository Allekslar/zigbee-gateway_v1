/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstdint>

#include "core_events.hpp"
#include "core_state.hpp"

namespace service {

class ReportingManager {
public:
    enum class State : uint8_t {
        kUnknown = 0,
        kPendingInterview,
        kPendingBind,
        kPendingConfigureReporting,
        kReportingActive,
        kDegraded,
    };

    struct RuntimeActions {
        bool request_interview{false};
        bool request_bind{false};
        bool request_configure_reporting{false};
        bool mark_degraded{false};
    };

    RuntimeActions handle_event(const core::CoreEvent& event) noexcept;
    bool get_state(uint16_t short_addr, State* out) const noexcept;
    uint32_t degraded_count() const noexcept;

private:
    struct Entry {
        uint16_t short_addr{core::kUnknownDeviceShortAddr};
        State state{State::kUnknown};
    };

    static bool valid_short_addr(uint16_t short_addr) noexcept;
    static int find_index(const std::array<Entry, core::kMaxDevices>& entries, uint16_t short_addr) noexcept;
    static int find_free_index(const std::array<Entry, core::kMaxDevices>& entries) noexcept;

    std::array<Entry, core::kMaxDevices> entries_{};
};

}  // namespace service
