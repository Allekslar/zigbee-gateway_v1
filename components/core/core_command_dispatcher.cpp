/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_commands.hpp"

namespace core {

namespace {

bool is_supported_command(CoreCommandType type) noexcept {
    switch (type) {
        case CoreCommandType::kSetDevicePower:
        case CoreCommandType::kRefreshNetwork:
            return true;
        case CoreCommandType::kUnknown:
        default:
            return false;
    }
}

CoreEventType map_result_to_event(CoreCommandResultType result_type) noexcept {
    switch (result_type) {
        case CoreCommandResultType::kSuccess:
            return CoreEventType::kCommandResultSuccess;
        case CoreCommandResultType::kTimeout:
            return CoreEventType::kCommandResultTimeout;
        case CoreCommandResultType::kFailed:
        default:
            return CoreEventType::kCommandResultFailed;
    }
}

}  // namespace

CoreEvent command_to_event(const CoreCommand& command) noexcept {
    CoreEvent event{};
    event.correlation_id = command.correlation_id;
    event.device_short_addr = command.device_short_addr;
    event.value_bool = command.desired_power_on;
    event.value_u32 = command.issued_at_ms;

    switch (command.type) {
        case CoreCommandType::kSetDevicePower:
            event.type = CoreEventType::kCommandSetDevicePowerRequested;
            break;
        case CoreCommandType::kRefreshNetwork:
            event.type = CoreEventType::kCommandRefreshNetworkRequested;
            break;
        case CoreCommandType::kUnknown:
        default:
            event.type = CoreEventType::kUnknown;
            break;
    }

    return event;
}

CoreError CoreCommandDispatcher::submit(const CoreCommand& command, CoreEvent* request_event_out) noexcept {
    if (request_event_out == nullptr) {
        return CoreError::kInvalidArgument;
    }

    if (command.correlation_id == kNoCorrelationId || !is_supported_command(command.type)) {
        return CoreError::kInvalidArgument;
    }

    if (pending_count_ >= kMaxPending) {
        return CoreError::kNoCapacity;
    }

    for (std::size_t i = 0; i < kMaxPending; ++i) {
        if (!pending_[i].in_use) {
            continue;
        }
        if (pending_[i].command.correlation_id == command.correlation_id) {
            return CoreError::kBusy;
        }
    }

    for (std::size_t i = 0; i < kMaxPending; ++i) {
        if (pending_[i].in_use) {
            continue;
        }

        pending_[i].in_use = true;
        pending_[i].command = command;
        ++pending_count_;
        *request_event_out = command_to_event(command);
        return CoreError::kOk;
    }

    return CoreError::kInternal;
}

CoreError CoreCommandDispatcher::resolve(
    const CoreCommandResult& result,
    CoreEvent* completion_event_out) noexcept {
    if (completion_event_out == nullptr || result.correlation_id == kNoCorrelationId) {
        return CoreError::kInvalidArgument;
    }

    for (std::size_t i = 0; i < kMaxPending; ++i) {
        PendingEntry& entry = pending_[i];
        if (!entry.in_use || entry.command.correlation_id != result.correlation_id) {
            continue;
        }

        completion_event_out->type = map_result_to_event(result.type);
        completion_event_out->correlation_id = result.correlation_id;
        completion_event_out->device_short_addr = entry.command.device_short_addr;
        completion_event_out->value_u32 = result.completed_at_ms;
        completion_event_out->value_bool = (result.type == CoreCommandResultType::kSuccess);

        entry = PendingEntry{};
        if (pending_count_ > 0) {
            --pending_count_;
        }
        return CoreError::kOk;
    }

    return CoreError::kNotFound;
}

std::size_t CoreCommandDispatcher::expire_timeouts(
    uint32_t now_ms,
    uint32_t timeout_ms,
    CoreEvent* events_out,
    std::size_t events_capacity) noexcept {
    if (events_out == nullptr || events_capacity == 0 || timeout_ms == 0) {
        return 0;
    }

    std::size_t produced = 0;
    for (std::size_t i = 0; i < kMaxPending && produced < events_capacity; ++i) {
        PendingEntry& entry = pending_[i];
        if (!entry.in_use) {
            continue;
        }

        const uint32_t age = now_ms - entry.command.issued_at_ms;
        if (age < timeout_ms) {
            continue;
        }

        CoreEvent timeout_event{};
        timeout_event.type = CoreEventType::kCommandResultTimeout;
        timeout_event.correlation_id = entry.command.correlation_id;
        timeout_event.device_short_addr = entry.command.device_short_addr;
        timeout_event.value_u32 = now_ms;
        timeout_event.value_bool = false;
        events_out[produced++] = timeout_event;

        entry = PendingEntry{};
        if (pending_count_ > 0) {
            --pending_count_;
        }
    }

    return produced;
}

std::size_t CoreCommandDispatcher::pending_count() const noexcept {
    return pending_count_;
}

}  // namespace core
