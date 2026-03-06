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
    assert(runtime.stats().reporting_retries == 1);
    assert(runtime.stats().reporting_failures == 0);
    assert(runtime.stats().stale_devices == 0);

    service::ServiceRuntime::DevicesApiSnapshot devices_snapshot{};
    assert(runtime.build_devices_api_snapshot(1000U, &devices_snapshot));
    assert(devices_snapshot.state.device_count == runtime.state().device_count);
    assert(!devices_snapshot.runtime.join_window_open);
    bool found_joined_device = false;
    for (std::size_t i = 0; i < devices_snapshot.state.devices.size(); ++i) {
        if (devices_snapshot.state.devices[i].short_addr == 0x3301) {
            found_joined_device = true;
            assert(devices_snapshot.runtime.force_remove_ms_left[i] == 0U);
            assert(devices_snapshot.runtime.reporting_state[i] == core::CoreReportingState::kUnknown);
            assert(devices_snapshot.runtime.last_report_at_ms[i] == 0U);
            assert(!devices_snapshot.runtime.stale[i]);
            assert(!devices_snapshot.runtime.has_battery[i]);
            assert(!devices_snapshot.runtime.has_battery_voltage[i]);
            assert(!devices_snapshot.runtime.has_lqi[i]);
            assert(!devices_snapshot.runtime.has_rssi[i]);
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

    core::CoreEvent stale{};
    stale.type = core::CoreEventType::kDeviceStale;
    stale.device_short_addr = 0x3301;
    assert(runtime.post_event(stale));
    assert(runtime.process_pending() == 1);
    assert(runtime.stats().reporting_failures == 1);
    assert(runtime.stats().stale_devices == 1);

    core::CoreEvent telemetry{};
    telemetry.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry.device_short_addr = 0x3301;
    telemetry.value_u32 = 7000;
    assert(runtime.post_event(telemetry));
    assert(runtime.process_pending() == 1);
    assert(runtime.stats().stale_devices == 0);

    // Runtime snapshot must expose reporting/telemetry fields for active devices.
    const core::CoreState base_state = runtime.state();
    core::CoreState telemetry_state = base_state;
    bool telemetry_device_found = false;
    for (auto& device : telemetry_state.devices) {
        if (device.short_addr == 0x3301 && device.online) {
            device.battery_percent = 77U;
            device.has_battery = true;
            device.battery_voltage_mv = 3000U;
            device.has_battery_voltage = true;
            device.lqi = 190U;
            device.has_lqi = true;
            device.rssi_dbm = -66;
            device.has_rssi = true;
            telemetry_device_found = true;
            break;
        }
    }
    assert(telemetry_device_found);
    assert(registry.publish(telemetry_state));

    service::ServiceRuntime::DevicesApiSnapshot telemetry_snapshot{};
    assert(runtime.build_devices_api_snapshot(8000U, &telemetry_snapshot));
    bool telemetry_verified = false;
    for (std::size_t i = 0; i < telemetry_snapshot.state.devices.size(); ++i) {
        const auto& device = telemetry_snapshot.state.devices[i];
        if (device.short_addr != 0x3301 || !device.online) {
            continue;
        }
        assert(telemetry_snapshot.runtime.reporting_state[i] == device.reporting_state);
        assert(telemetry_snapshot.runtime.last_report_at_ms[i] == device.last_report_at_ms);
        assert(telemetry_snapshot.runtime.stale[i] == device.stale);
        assert(telemetry_snapshot.runtime.has_battery[i]);
        assert(telemetry_snapshot.runtime.battery_percent[i] == 77U);
        assert(telemetry_snapshot.runtime.has_battery_voltage[i]);
        assert(telemetry_snapshot.runtime.battery_voltage_mv[i] == 3000U);
        assert(telemetry_snapshot.runtime.has_lqi[i]);
        assert(telemetry_snapshot.runtime.lqi[i] == 190U);
        assert(telemetry_snapshot.runtime.has_rssi[i]);
        assert(telemetry_snapshot.runtime.rssi_dbm[i] == -66);
        telemetry_verified = true;
        break;
    }
    assert(telemetry_verified);

    core::CoreCommand bad_reporting_cmd{};
    bad_reporting_cmd.type = core::CoreCommandType::kUpdateReportingProfile;
    bad_reporting_cmd.correlation_id = 300U;
    bad_reporting_cmd.device_short_addr = 0x3301U;
    bad_reporting_cmd.reporting_endpoint = 1U;
    bad_reporting_cmd.reporting_cluster_id = 0x0402U;
    bad_reporting_cmd.reporting_min_interval_seconds = 301U;
    bad_reporting_cmd.reporting_max_interval_seconds = 300U;
    assert(runtime.post_command(bad_reporting_cmd) == core::CoreError::kInvalidArgument);

    core::CoreCommand reporting_cmd{};
    reporting_cmd.type = core::CoreCommandType::kUpdateReportingProfile;
    reporting_cmd.correlation_id = 301U;
    reporting_cmd.device_short_addr = 0x3301U;
    reporting_cmd.reporting_endpoint = 1U;
    reporting_cmd.reporting_cluster_id = 0x0402U;
    reporting_cmd.reporting_min_interval_seconds = 10U;
    reporting_cmd.reporting_max_interval_seconds = 300U;
    reporting_cmd.reporting_reportable_change = 25U;
    reporting_cmd.reporting_capability_flags = 3U;
    assert(runtime.post_command(reporting_cmd) == core::CoreError::kOk);

    service::ConfigManager::ReportingProfileKey reporting_key{};
    reporting_key.short_addr = 0x3301U;
    reporting_key.endpoint = 1U;
    reporting_key.cluster_id = 0x0402U;
    service::ConfigManager::ReportingProfile reporting_before{};
    assert(!runtime.config_manager().get_reporting_profile(reporting_key, &reporting_before));
    (void)runtime.process_pending();
    service::ConfigManager::ReportingProfile reporting_after{};
    assert(runtime.config_manager().get_reporting_profile(reporting_key, &reporting_after));
    assert(reporting_after.min_interval_seconds == 10U);
    assert(reporting_after.max_interval_seconds == 300U);
    assert(reporting_after.reportable_change == 25U);
    assert(reporting_after.capability_flags == 3U);

    // Idempotent command path: same profile is accepted and does not enqueue extra work.
    reporting_cmd.correlation_id = 302U;
    assert(runtime.post_command(reporting_cmd) == core::CoreError::kOk);
    assert(runtime.process_pending() == 0U);

    return 0;
}
