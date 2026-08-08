/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "config_manager.hpp"
#include "core_registry.hpp"
#include "device_id.hpp"
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
    assert(bridge.start());

    const core::DeviceId reporting_device_id = [] {
        core::DeviceId id{};
        assert(core::DeviceId::parse("00124b0001aa8705", 16, &id));
        return id;
    }();

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_id = reporting_device_id;
    joined.device_short_addr = 8705U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);
    assert(bridge.sync_runtime_snapshot() >= 1U);
    mqtt_bridge::MqttPublishedMessage drained[16]{};
    (void)bridge.drain_publications(drained, 16U);

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

    // A short_addr with no resolvable locator is rejected rather than
    // written under a synthetic identity (FD-01/FD-03). CoreState carries
    // a valid device_id for 8705 (set on the join event above), but the
    // legacy config-command path resolves identity via the *locator
    // registry* (resolve_device_id_for_short_addr), which has no entry for
    // 8705 until the explicit remap() call below.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}",
        5025U));

    uint32_t reporting_device_revision = 0U;
    assert(runtime.device_locator_registry().remap(reporting_device_id, 8705U, &reporting_device_revision) ==
           service::DeviceLocatorRemapResult::kInserted);

    // Valid command should enqueue write through ServiceRuntime command path.
    assert(bridge.handle_command_message(
        "zigbee-gateway/devices/8705/config",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300,\"reportable_change\":25,\"capability_flags\":3}",
        503U));

    service::ConfigManager::ReportingProfileKey key{};
    key.device_id = reporting_device_id;
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

    // --- Versioned (v1) config command, same device, DeviceId-keyed topic. ---
    assert(bridge.handle_command_message(
        "zigbee-gateway/v1/devices/00124b0001aa8705/config/set",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":20,\"max_interval_seconds\":600,"
        "\"reportable_change\":50,\"capability_flags\":1}",
        5030U));
    (void)runtime.process_pending();
    assert(runtime.config_manager().get_reporting_profile(key, &profile));
    assert(profile.min_interval_seconds == 20U);
    assert(profile.max_interval_seconds == 600U);

    // v1 config command with a malformed device_id segment is rejected.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/v1/devices/not-16-hex-chars/config/set",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
        5031U));

    // v1 config command for a device_id with no CoreState record at all.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/v1/devices/00124b0001aaffff/config/set",
        "{\"endpoint\":1,\"cluster_id\":1026,\"min_interval_seconds\":10,\"max_interval_seconds\":300}",
        5032U));

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
    const std::size_t immediate_publications = bridge.drain_publications(drained, 16U);
    assert(immediate_publications >= 1U);
    bool has_power_state = false;
    for (std::size_t i = 0; i < immediate_publications; ++i) {
        if (std::strcmp(drained[i].topic, "zigbee-gateway/v1/devices/00124b0001aa8705/state") == 0) {
            assert(std::strstr(drained[i].payload, "\"power_on\":true") != nullptr);
            has_power_state = true;
        }
    }
    assert(has_power_state);
    (void)bridge.sync_runtime_snapshot();
    const std::size_t post_sync_publications = bridge.drain_publications(drained, 16U);
    for (std::size_t i = 0; i < post_sync_publications; ++i) {
        if (std::strcmp(drained[i].topic, "zigbee-gateway/v1/devices/00124b0001aa8705/state") == 0) {
            assert(std::strstr(drained[i].payload, "\"power_on\":false") == nullptr);
        }
    }

    // Simulate the task-loop order used in production: publish pending first, then sync, then publish again.
    assert(bridge.handle_command_message(
        "zigbee-gateway/devices/8705/power/set",
        "{\"power_on\":false}",
        507U));
    const std::size_t optimistic_publications = bridge.drain_publications(drained, 16U);
    bool saw_false_state = false;
    for (std::size_t i = 0; i < optimistic_publications; ++i) {
        if (std::strcmp(drained[i].topic, "zigbee-gateway/v1/devices/00124b0001aa8705/state") == 0) {
            assert(std::strstr(drained[i].payload, "\"power_on\":false") != nullptr);
            saw_false_state = true;
        }
    }
    assert(saw_false_state);

    // --- v1 power command, same device. ---
    assert(bridge.handle_command_message(
        "zigbee-gateway/v1/devices/00124b0001aa8705/power/set", "{\"power_on\":true}", 508U));
    const std::size_t v1_power_publications = bridge.drain_publications(drained, 16U);
    bool saw_v1_true_state = false;
    for (std::size_t i = 0; i < v1_power_publications; ++i) {
        if (std::strcmp(drained[i].topic, "zigbee-gateway/v1/devices/00124b0001aa8705/state") == 0) {
            assert(std::strstr(drained[i].payload, "\"power_on\":true") != nullptr);
            saw_v1_true_state = true;
        }
    }
    assert(saw_v1_true_state);

    // v1 power command for an unknown device_id is rejected.
    assert(!bridge.handle_command_message(
        "zigbee-gateway/v1/devices/00124b0001aaffff/power/set", "{\"power_on\":true}", 509U));

    assert(runtime.process_pending() >= 1U);
    assert(runtime.pending_commands() == 3U);

    core::CoreCommandResult result{};
    result.correlation_id = 506U;
    result.type = core::CoreCommandResultType::kSuccess;
    result.completed_at_ms = 1000U;
    assert(runtime.handle_command_result(result) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 2U);

    core::CoreCommandResult second_result{};
    second_result.correlation_id = 507U;
    second_result.type = core::CoreCommandResultType::kSuccess;
    second_result.completed_at_ms = 1001U;
    assert(runtime.handle_command_result(second_result) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 1U);

    core::CoreCommandResult third_result{};
    third_result.correlation_id = 508U;
    third_result.type = core::CoreCommandResultType::kSuccess;
    third_result.completed_at_ms = 1002U;
    assert(runtime.handle_command_result(third_result) == core::CoreError::kOk);
    assert(runtime.process_pending() == 1U);
    assert(runtime.pending_commands() == 0U);
    assert(runtime.state().last_command_status == 1U);

    return 0;
}
