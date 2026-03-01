/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core_errors.hpp"
#include "core_events.hpp"

namespace core {

enum class CoreCommandType : uint8_t {
    kUnknown = 0,
    kSetDevicePower,
    kRefreshNetwork,
};

struct CoreCommand {
    CoreCommandType type{CoreCommandType::kUnknown};
    uint32_t correlation_id{kNoCorrelationId};
    uint16_t device_short_addr{kUnknownDeviceShortAddr};
    bool desired_power_on{false};
    uint32_t issued_at_ms{0};
};

enum class CoreCommandResultType : uint8_t {
    kSuccess = 0,
    kTimeout,
    kFailed,
};

struct CoreCommandResult {
    uint32_t correlation_id{kNoCorrelationId};
    CoreCommandResultType type{CoreCommandResultType::kFailed};
    uint32_t completed_at_ms{0};
};

class CoreCommandDispatcher {
public:
    static constexpr std::size_t kMaxPending = 16;

    CoreError submit(const CoreCommand& command, CoreEvent* request_event_out) noexcept;
    CoreError resolve(const CoreCommandResult& result, CoreEvent* completion_event_out) noexcept;
    std::size_t expire_timeouts(
        uint32_t now_ms,
        uint32_t timeout_ms,
        CoreEvent* events_out,
        std::size_t events_capacity) noexcept;
    std::size_t pending_count() const noexcept;

private:
    struct PendingEntry {
        bool in_use{false};
        CoreCommand command{};
    };

    std::array<PendingEntry, kMaxPending> pending_{};
    std::size_t pending_count_{0};
};

CoreEvent command_to_event(const CoreCommand& command) noexcept;

}  // namespace core
