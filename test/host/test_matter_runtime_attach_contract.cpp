/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "matter_bridge.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    matter_bridge::MatterBridge bridge;
    assert(bridge.start());

    // Runtime is not attached yet.
    assert(bridge.sync_runtime_snapshot() == 0U);
    assert(bridge.post_power_command(0x2201U, true, 0U, nullptr) == service::CommandSubmitStatus::kInvalidArgument);

    const matter_bridge::MatterEndpointMapEntry map[] = {{0x2201U, 50U}};
    assert(bridge.set_endpoint_map(map, 1U));
    bridge.attach_runtime(&runtime);

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);
    assert(bridge.sync_runtime_snapshot() == 2U);

    // Unknown short address is rejected at the bridge boundary.
    assert(
        bridge.post_power_command(core::kUnknownDeviceShortAddr, true, 10U, nullptr) ==
        service::CommandSubmitStatus::kInvalidArgument);

    bridge.attach_runtime(nullptr);
    assert(bridge.sync_runtime_snapshot() == 0U);

    bridge.stop();
    return 0;
}
