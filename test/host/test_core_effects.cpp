/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>
#include <cstdint>

#include "core_commands.hpp"
#include "core_state.hpp"

int main() {
    core::CoreCommandDispatcher dispatcher;
    assert(dispatcher.pending_count() == 0);

    core::CoreCommand invalid{};
    invalid.type = core::CoreCommandType::kSetDevicePower;
    invalid.correlation_id = core::kNoCorrelationId;
    core::CoreEvent ignored{};
    assert(dispatcher.submit(invalid, &ignored) == core::CoreError::kInvalidArgument);

    core::CoreCommand cmd1{};
    cmd1.type = core::CoreCommandType::kSetDevicePower;
    cmd1.correlation_id = 1;
    cmd1.device_short_addr = 0x1001;
    cmd1.desired_power_on = true;
    cmd1.issued_at_ms = 1000;

    core::CoreEvent request_event{};
    assert(dispatcher.submit(cmd1, &request_event) == core::CoreError::kOk);
    assert(dispatcher.pending_count() == 1);
    assert(request_event.type == core::CoreEventType::kCommandSetDevicePowerRequested);
    assert(request_event.correlation_id == 1);
    assert(request_event.device_short_addr == 0x1001);
    assert(request_event.value_bool);

    core::CoreEvent duplicate_event{};
    assert(dispatcher.submit(cmd1, &duplicate_event) == core::CoreError::kBusy);

    core::CoreCommandResult success{};
    success.correlation_id = 1;
    success.type = core::CoreCommandResultType::kSuccess;
    success.completed_at_ms = 1200;

    core::CoreEvent completion{};
    assert(dispatcher.resolve(success, &completion) == core::CoreError::kOk);
    assert(dispatcher.pending_count() == 0);
    assert(completion.type == core::CoreEventType::kCommandResultSuccess);
    assert(completion.correlation_id == 1);
    assert(completion.device_short_addr == 0x1001);
    assert(completion.value_bool);

    core::CoreCommand cmd2{};
    cmd2.type = core::CoreCommandType::kRefreshNetwork;
    cmd2.correlation_id = 2;
    cmd2.issued_at_ms = 1000;
    assert(dispatcher.submit(cmd2, &request_event) == core::CoreError::kOk);
    assert(request_event.type == core::CoreEventType::kCommandRefreshNetworkRequested);

    core::CoreCommand cmd3{};
    cmd3.type = core::CoreCommandType::kSetDevicePower;
    cmd3.correlation_id = 3;
    cmd3.device_short_addr = 0x2002;
    cmd3.desired_power_on = false;
    cmd3.issued_at_ms = 4500;
    assert(dispatcher.submit(cmd3, &request_event) == core::CoreError::kOk);
    assert(dispatcher.pending_count() == 2);

    std::array<core::CoreEvent, 4> timeout_events{};
    const std::size_t expired = dispatcher.expire_timeouts(5000, 2000, timeout_events.data(), timeout_events.size());
    assert(expired == 1);
    assert(timeout_events[0].type == core::CoreEventType::kCommandResultTimeout);
    assert(timeout_events[0].correlation_id == 2);
    assert(dispatcher.pending_count() == 1);

    core::CoreCommandResult failed{};
    failed.correlation_id = 3;
    failed.type = core::CoreCommandResultType::kFailed;
    failed.completed_at_ms = 5050;
    assert(dispatcher.resolve(failed, &completion) == core::CoreError::kOk);
    assert(completion.type == core::CoreEventType::kCommandResultFailed);
    assert(completion.correlation_id == 3);
    assert(!completion.value_bool);
    assert(dispatcher.pending_count() == 0);

    core::CoreCommandResult unknown{};
    unknown.correlation_id = 999;
    unknown.type = core::CoreCommandResultType::kSuccess;
    assert(dispatcher.resolve(unknown, &completion) == core::CoreError::kNotFound);

    core::CoreState base{};
    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x3344;
    const core::CoreReduceResult joined_reduce = core::core_reduce(base, joined);
    assert(joined_reduce.effects.count == 3);
    assert(joined_reduce.effects.items[2].type == core::CoreEffectType::kZigbeeInterview);

    core::CoreEvent interview_done{};
    interview_done.type = core::CoreEventType::kDeviceInterviewCompleted;
    interview_done.device_short_addr = 0x3344;
    const core::CoreReduceResult bind_reduce = core::core_reduce(joined_reduce.next, interview_done);
    assert(bind_reduce.effects.count == 3);
    assert(bind_reduce.effects.items[2].type == core::CoreEffectType::kZigbeeBind);

    core::CoreEvent bind_done{};
    bind_done.type = core::CoreEventType::kDeviceBindingReady;
    bind_done.device_short_addr = 0x3344;
    const core::CoreReduceResult cfg_reduce = core::core_reduce(bind_reduce.next, bind_done);
    assert(cfg_reduce.effects.count == 3);
    assert(cfg_reduce.effects.items[2].type == core::CoreEffectType::kZigbeeConfigureReporting);

    core::CoreEvent cfg_done{};
    cfg_done.type = core::CoreEventType::kDeviceReportingConfigured;
    cfg_done.device_short_addr = 0x3344;
    const core::CoreReduceResult read_reduce = core::core_reduce(cfg_reduce.next, cfg_done);
    assert(read_reduce.effects.count == 3);
    assert(read_reduce.effects.items[2].type == core::CoreEffectType::kZigbeeReadAttributes);

    return 0;
}
