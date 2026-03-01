/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core_commands.hpp"

namespace service {

class ServiceRuntime;

class CommandManager {
public:
    static constexpr std::size_t kCommandIngressQueueCapacity = core::CoreCommandDispatcher::kMaxPending;
    static constexpr std::size_t kCommandResultQueueCapacity = core::CoreCommandDispatcher::kMaxPending;

    core::CoreError post_command(ServiceRuntime& runtime, const core::CoreCommand& command) noexcept;
    core::CoreError handle_command_result(ServiceRuntime& runtime, const core::CoreCommandResult& result) noexcept;

    bool drain_command_requests(ServiceRuntime& runtime) noexcept;
    bool drain_command_results(ServiceRuntime& runtime) noexcept;
    std::size_t process_timeouts(ServiceRuntime& runtime, uint32_t now_ms) noexcept;

    std::size_t pending_ingress_count() const noexcept;
    std::size_t pending_commands() const noexcept;

private:
    struct TrackedCommand {
        bool in_use{false};
        core::CoreCommand command{};
        uint8_t retries_left{0};
    };

    struct CommandIngressNotification {
        core::CoreCommand command{};
    };

    class SpinLockGuard {
    public:
        explicit SpinLockGuard(std::atomic_flag& lock) noexcept;
        ~SpinLockGuard() noexcept;

    private:
        std::atomic_flag& lock_;
    };

    bool queue_command_request(const CommandIngressNotification& notification) noexcept;
    bool pop_command_request(CommandIngressNotification* out) noexcept;
    bool queue_command_result(const core::CoreCommandResult& result) noexcept;
    bool pop_command_result(core::CoreCommandResult* out) noexcept;

    TrackedCommand* find_tracked_command(uint32_t correlation_id) noexcept;
    TrackedCommand* allocate_tracked_command() noexcept;
    void release_tracked_command(uint32_t correlation_id) noexcept;

    void refresh_pending_commands_cache() noexcept;
    core::CoreError submit_command_internal(ServiceRuntime& runtime, const core::CoreCommand& command) noexcept;
    core::CoreError resolve_command_result(ServiceRuntime& runtime, const core::CoreCommandResult& result) noexcept;

    mutable std::atomic_flag queue_lock_ = ATOMIC_FLAG_INIT;

    std::array<CommandIngressNotification, kCommandIngressQueueCapacity> command_request_queue_{};
    std::size_t command_request_head_{0};
    std::size_t command_request_tail_{0};
    std::size_t command_request_count_{0};

    std::array<core::CoreCommandResult, kCommandResultQueueCapacity> command_result_queue_{};
    std::size_t command_result_head_{0};
    std::size_t command_result_tail_{0};
    std::size_t command_result_count_{0};

    std::array<TrackedCommand, core::CoreCommandDispatcher::kMaxPending> tracked_commands_{};
    core::CoreCommandDispatcher command_dispatcher_{};
    std::atomic<uint32_t> pending_dispatcher_commands_cache_{0};
};

}  // namespace service
