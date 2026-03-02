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
    static constexpr uint8_t kMaxRetryAttempts = 3U;
    static constexpr uint32_t kBaseBackoffMs = 1000U;
    static constexpr uint32_t kMaxBackoffMs = 8000U;

    enum class State : uint8_t {
        kUnknown = 0,
        kPendingInterview,
        kPendingBind,
        kPendingConfigureReporting,
        kReportingActive,
        kDegraded,
    };

    enum class RetryTarget : uint8_t {
        kNone = 0,
        kBind,
        kConfigureReporting,
    };

    enum class FailureReason : uint8_t {
        kNone = 0,
        kTimeout,
        kNotSupported,
        kNetworkError,
        kUnsupportedAttr,
    };

    struct RuntimeActions {
        bool request_interview{false};
        bool request_bind{false};
        bool request_configure_reporting{false};
        bool mark_degraded{false};
    };

    struct RetryStatus {
        RetryTarget target{RetryTarget::kNone};
        FailureReason reason{FailureReason::kNone};
        uint8_t attempt{0};
        uint32_t next_retry_at_ms{0};
        bool pending{false};
    };

    RuntimeActions handle_event(const core::CoreEvent& event) noexcept;
    RuntimeActions report_operation_failure(
        uint16_t short_addr,
        RetryTarget target,
        FailureReason reason,
        uint32_t now_ms) noexcept;
    RuntimeActions process_timeouts(uint32_t now_ms) noexcept;
    bool get_state(uint16_t short_addr, State* out) const noexcept;
    bool get_retry_status(uint16_t short_addr, RetryStatus* out) const noexcept;
    uint32_t degraded_count() const noexcept;

private:
    struct Entry {
        uint16_t short_addr{core::kUnknownDeviceShortAddr};
        State state{State::kUnknown};
        RetryTarget retry_target{RetryTarget::kNone};
        FailureReason last_failure_reason{FailureReason::kNone};
        uint8_t retry_attempt{0};
        uint32_t next_retry_at_ms{0};
        bool retry_pending{false};
    };

    static bool valid_short_addr(uint16_t short_addr) noexcept;
    static int find_index(const std::array<Entry, core::kMaxDevices>& entries, uint16_t short_addr) noexcept;
    static int find_free_index(const std::array<Entry, core::kMaxDevices>& entries) noexcept;
    static bool is_retryable_reason(FailureReason reason) noexcept;
    static uint32_t compute_backoff_ms(uint8_t attempt) noexcept;
    static bool is_due(uint32_t now_ms, uint32_t deadline_ms) noexcept;
    static void clear_retry_state(Entry* entry) noexcept;

    std::array<Entry, core::kMaxDevices> entries_{};
};

}  // namespace service
