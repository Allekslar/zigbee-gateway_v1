/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "config_manager.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "mqtt_bridge.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    mqtt_bridge::MqttBridge bridge;

    // Runtime must be attached.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
        500U));

    bridge.attach_runtime(&runtime);

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 8705U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);

    // Invalid topic.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/devices/8705/state",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
        501U));

    // Invalid payload bounds.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":301,\"max_interval_seconds\":300}",
        502U));

    // Valid command should enqueue write through ServiceRuntime command path.
    assert(bridge.handle_command_message(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}",
        503U));

    service::ConfigManager::ReportingProfileKey key{};
    key.short_addr = 8705U;
    key.endpoint = 1U;
    key.cluster_id = 1026U;

    service::ConfigManager::ReportingProfile profile{};
    assert(!runtime.config_manager().get_reporting_profile(key, &profile));

    (void)runtime.process_pending();
    assert(runtime.config_manager().get_reporting_profile(key, &profile));
    assert(profile.min_interval_seconds == 10U);
    assert(profile.max_interval_seconds == 300U);
    assert(profile.reportable_change == 25U);
    assert(profile.capability_flags == 3U);

    // Idempotent command: accepted but no additional queue work.
    assert(bridge.handle_command_message(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}",
        504U));
    assert(runtime.process_pending() == 0U);

    // Invalid power payload.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/devices/8705/power/set",
        "{\"power_on\":17}",
        505U));

    // Valid power command should enter normal runtime command ingress.
    assert(bridge.handle_command_message(
        "zigbee-gateway/devices/8705/power/set",
        "{\"power_on\":true}",
        506U));
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 1U);

    core::CoreCommandResult result{};
    result.correlation_id = 506U;
    result.type = core::CoreCommandResultType::kSuccess;
    result.completed_at_ms = 1000U;
    assert(runtime.handle_command_result(result) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 0U);
    assert(runtime.state().last_command_status == 1U);

    return 0;
}
