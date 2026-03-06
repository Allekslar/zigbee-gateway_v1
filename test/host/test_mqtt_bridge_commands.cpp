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
    assert(!bridge.handle_config_command(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
        500U));

    bridge.attach_runtime(&runtime);

    // Invalid topic.
    assert(!bridge.handle_config_command(
        "zigbee-gateway/devices/8705/state",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
        501U));

    // Invalid payload bounds.
    assert(!bridge.handle_config_command(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":301,\"max_interval_seconds\":300}",
        502U));

    // Valid command should enqueue write through ServiceRuntime command path.
    assert(bridge.handle_config_command(
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
    assert(bridge.handle_config_command(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}",
        504U));
    assert(runtime.process_pending() == 0U);

    return 0;
}
