/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "command_manager.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <thread>
#endif

#include "service_runtime.hpp"

namespace service {

CommandManager::SpinLockGuard::SpinLockGuard(std::atomic_flag& lock) noexcept : lock_(lock) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
#ifdef ESP_PLATFORM
        taskYIELD();
#else
        std::this_thread::yield();
#endif
    }
}

CommandManager::SpinLockGuard::~SpinLockGuard() noexcept {
    lock_.clear(std::memory_order_release);
}

core::CoreError CommandManager::post_command(ServiceRuntime& runtime, const core::CoreCommand& command) noexcept {
    if (command.correlation_id == core::kNoCorrelationId || command.type == core::CoreCommandType::kUnknown) {
        return core::CoreError::kInvalidArgument;
    }

    CommandIngressNotification notification{};
    notification.command = command;
    if (!queue_command_request(notification)) {
        (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
        return core::CoreError::kNoCapacity;
    }

    return core::CoreError::kOk;
}

core::CoreError CommandManager::handle_command_result(
    ServiceRuntime& runtime,
    const core::CoreCommandResult& result) noexcept {
    if (result.correlation_id == core::kNoCorrelationId) {
        return core::CoreError::kInvalidArgument;
    }

    if (!queue_command_result(result)) {
        (void)runtime.dropped_ingress_events_.fetch_add(1, std::memory_order_relaxed);
        return core::CoreError::kNoCapacity;
    }

    return core::CoreError::kOk;
}

bool CommandManager::queue_command_request(const CommandIngressNotification& notification) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (command_request_count_ >= kCommandIngressQueueCapacity) {
        return false;
    }

    command_request_queue_[command_request_tail_] = notification;
    command_request_tail_ = (command_request_tail_ + 1U) % kCommandIngressQueueCapacity;
    ++command_request_count_;
    return true;
}

bool CommandManager::pop_command_request(CommandIngressNotification* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || command_request_count_ == 0) {
        return false;
    }

    *out = command_request_queue_[command_request_head_];
    command_request_head_ = (command_request_head_ + 1U) % kCommandIngressQueueCapacity;
    --command_request_count_;
    return true;
}

bool CommandManager::queue_command_result(const core::CoreCommandResult& result) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (command_result_count_ >= kCommandResultQueueCapacity) {
        return false;
    }

    command_result_queue_[command_result_tail_] = result;
    command_result_tail_ = (command_result_tail_ + 1U) % kCommandResultQueueCapacity;
    ++command_result_count_;
    return true;
}

bool CommandManager::pop_command_result(core::CoreCommandResult* out) noexcept {
    SpinLockGuard guard(queue_lock_);
    if (out == nullptr || command_result_count_ == 0) {
        return false;
    }

    *out = command_result_queue_[command_result_head_];
    command_result_head_ = (command_result_head_ + 1U) % kCommandResultQueueCapacity;
    --command_result_count_;
    return true;
}

CommandManager::TrackedCommand* CommandManager::find_tracked_command(uint32_t correlation_id) noexcept {
    if (correlation_id == core::kNoCorrelationId) {
        return nullptr;
    }

    for (std::size_t i = 0; i < tracked_commands_.size(); ++i) {
        TrackedCommand& tracked = tracked_commands_[i];
        if (tracked.in_use && tracked.command.correlation_id == correlation_id) {
            return &tracked;
        }
    }

    return nullptr;
}

CommandManager::TrackedCommand* CommandManager::allocate_tracked_command() noexcept {
    for (std::size_t i = 0; i < tracked_commands_.size(); ++i) {
        TrackedCommand& tracked = tracked_commands_[i];
        if (!tracked.in_use) {
            tracked = TrackedCommand{};
            tracked.in_use = true;
            return &tracked;
        }
    }

    return nullptr;
}

void CommandManager::release_tracked_command(uint32_t correlation_id) noexcept {
    TrackedCommand* tracked = find_tracked_command(correlation_id);
    if (tracked != nullptr) {
        *tracked = TrackedCommand{};
    }
}

void CommandManager::refresh_pending_commands_cache() noexcept {
    pending_dispatcher_commands_cache_.store(
        static_cast<uint32_t>(command_dispatcher_.pending_count()),
        std::memory_order_release);
}

core::CoreError CommandManager::submit_command_internal(
    ServiceRuntime& runtime,
    const core::CoreCommand& command) noexcept {
    core::CoreEvent request_event{};
    const core::CoreError submit_err = command_dispatcher_.submit(command, &request_event);
    if (submit_err != core::CoreError::kOk) {
        return submit_err;
    }
    refresh_pending_commands_cache();

    TrackedCommand* tracked = find_tracked_command(command.correlation_id);
    if (tracked == nullptr) {
        tracked = allocate_tracked_command();
    }
    if (tracked == nullptr) {
        return core::CoreError::kNoCapacity;
    }

    tracked->command = command;
    tracked->retries_left = runtime.config_manager_.max_command_retries();

    if (!runtime.push_event(request_event)) {
        return core::CoreError::kNoCapacity;
    }

    return core::CoreError::kOk;
}

core::CoreError CommandManager::resolve_command_result(
    ServiceRuntime& runtime,
    const core::CoreCommandResult& result) noexcept {
    core::CoreEvent completion_event{};
    const core::CoreError resolve_err = command_dispatcher_.resolve(result, &completion_event);
    if (resolve_err != core::CoreError::kOk) {
        return resolve_err;
    }
    refresh_pending_commands_cache();

    release_tracked_command(result.correlation_id);

    if (!runtime.push_event(completion_event)) {
        return core::CoreError::kNoCapacity;
    }

    return core::CoreError::kOk;
}

bool CommandManager::drain_command_requests(ServiceRuntime& runtime) noexcept {
    bool drained = false;
    CommandIngressNotification notification{};

    while (pop_command_request(&notification)) {
        drained = true;
        const core::CoreError err = submit_command_internal(runtime, notification.command);
        if (err != core::CoreError::kOk) {
            ++runtime.stats_.dropped_events;
        }
    }

    return drained;
}

bool CommandManager::drain_command_results(ServiceRuntime& runtime) noexcept {
    bool drained = false;
    core::CoreCommandResult result{};

    while (pop_command_result(&result)) {
        drained = true;
        const core::CoreError err = resolve_command_result(runtime, result);
        if (err != core::CoreError::kOk) {
            ++runtime.stats_.dropped_events;
        }
    }

    return drained;
}

std::size_t CommandManager::process_timeouts(ServiceRuntime& runtime, uint32_t now_ms) noexcept {
    std::array<core::CoreEvent, core::CoreCommandDispatcher::kMaxPending> timeout_events{};
    const std::size_t expired = command_dispatcher_.expire_timeouts(
        now_ms,
        runtime.config_manager_.command_timeout_ms(),
        timeout_events.data(),
        timeout_events.size());

    std::size_t queued = 0;
    for (std::size_t i = 0; i < expired; ++i) {
        const core::CoreEvent& timeout_event = timeout_events[i];
        TrackedCommand* tracked = find_tracked_command(timeout_event.correlation_id);
        if (tracked != nullptr && tracked->retries_left > 0) {
            --tracked->retries_left;
            tracked->command.issued_at_ms = now_ms;
            ++runtime.stats_.command_retries;

            core::CoreEvent retry_request{};
            const core::CoreError retry_submit = command_dispatcher_.submit(tracked->command, &retry_request);
            if (retry_submit == core::CoreError::kOk && runtime.push_event(retry_request)) {
                ++queued;
                continue;
            }
        }

        ++runtime.stats_.command_timeouts;
        release_tracked_command(timeout_event.correlation_id);
        if (runtime.push_event(timeout_event)) {
            ++queued;
        }
    }

    refresh_pending_commands_cache();
    return queued;
}

std::size_t CommandManager::pending_ingress_count() const noexcept {
    SpinLockGuard guard(queue_lock_);
    return command_request_count_ + command_result_count_;
}

std::size_t CommandManager::pending_commands() const noexcept {
    SpinLockGuard guard(queue_lock_);
    return pending_dispatcher_commands_cache_.load(std::memory_order_acquire) + command_request_count_;
}

}  // namespace service
