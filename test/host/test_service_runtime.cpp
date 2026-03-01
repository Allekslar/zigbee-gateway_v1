/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_commands.hpp"
#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x3301;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1);
    assert(runtime.state().device_count == 1);

    service::ServiceRuntime::DevicesApiSnapshot devices_snapshot{};
    assert(runtime.build_devices_api_snapshot(1000U, &devices_snapshot));
    assert(devices_snapshot.state.device_count == runtime.state().device_count);
    assert(!devices_snapshot.runtime.join_window_open);
    bool found_joined_device = false;
    for (std::size_t i = 0; i < devices_snapshot.state.devices.size(); ++i) {
        if (devices_snapshot.state.devices[i].short_addr == 0x3301) {
            found_joined_device = true;
            assert(devices_snapshot.runtime.force_remove_ms_left[i] == 0U);
            break;
        }
    }
    assert(found_joined_device);

    runtime.config_manager().set_command_timeout_ms(1000);
    runtime.config_manager().set_max_command_retries(1);

    core::CoreCommand cmd{};
    cmd.type = core::CoreCommandType::kSetDevicePower;
    cmd.correlation_id = 100;
    cmd.device_short_addr = 0x3301;
    cmd.desired_power_on = true;
    cmd.issued_at_ms = 1000;

    assert(runtime.submit_command(cmd) == core::CoreError::kOk);
    assert(runtime.pending_commands() == 1);
    assert(runtime.process_pending() == 1);

    // First timeout should trigger retry (not final timeout event).
    assert(runtime.tick(2500) == 1);
    assert(runtime.stats().command_retries == 1);
    assert(runtime.pending_commands() == 1);
    assert(runtime.process_pending() == 1);

    // Second timeout should become final timeout event.
    assert(runtime.tick(4500) == 1);
    assert(runtime.pending_commands() == 0);
    assert(runtime.process_pending() == 1);
    assert(runtime.state().last_command_status == 2);
    assert(runtime.stats().command_timeouts == 1);

    core::CoreCommand cmd_ok{};
    cmd_ok.type = core::CoreCommandType::kRefreshNetwork;
    cmd_ok.correlation_id = 200;
    cmd_ok.issued_at_ms = 5000;
    assert(runtime.submit_command(cmd_ok) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1);

    core::CoreCommandResult ok_result{};
    ok_result.correlation_id = 200;
    ok_result.type = core::CoreCommandResultType::kSuccess;
    ok_result.completed_at_ms = 5100;
    assert(runtime.handle_command_result(ok_result) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1);
    assert(runtime.state().last_command_status == 1);
    assert(runtime.pending_commands() == 0);

    return 0;
}
