/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "matter_bridge.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    matter_bridge::MatterBridge bridge;
    assert(bridge.start());

    // Runtime is not attached yet.
    assert(bridge.sync_runtime_snapshot() == 0U);
    assert(bridge.post_power_command(0x2201U, true, 0U, nullptr) == service::CommandSubmitStatus::kInvalidArgument);

    bridge.attach_runtime(&runtime);

    // A resolved DeviceId is required for MatterEndpointRegistry to
    // allocate an endpoint (plan S4 #20: Matter identity is DeviceId-keyed,
    // never short_addr-keyed).
    const std::array<uint8_t, HAL_ZIGBEE_IEEE_ADDR_LEN> ieee_a = {
        0x00, 0x12, 0x4b, 0x00, 0x01, 0xaa, 0x22, 0x01};
    hal_zigbee_simulate_device_joined_with_identity(0x2201U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);
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
