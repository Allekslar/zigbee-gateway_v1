/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_commands.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_wifi.h"
#include "hal_zigbee.h"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    hal_wifi_simulate_network_up();
    assert(runtime.process_pending() == 1);
    assert(runtime.stats().nvs_writes >= 1);
    assert(runtime.stats().last_nvs_revision >= 1);

    hal_zigbee_simulate_device_joined(0x2201);
    hal_zigbee_simulate_attribute_report(0x2201, 0x0006, 0x0000, true, 1);
    assert(runtime.process_pending() == 2);

    const auto state = runtime.state();
    assert(state.device_count == 1);
    bool found = false;
    for (const auto& dev : state.devices) {
        if (dev.short_addr == 0x2201 && dev.online) {
            assert(dev.power_on);
            found = true;
        }
    }
    assert(found);

    core::CoreCommand cmd{};
    cmd.type = core::CoreCommandType::kSetDevicePower;
    cmd.correlation_id = 300;
    cmd.device_short_addr = 0x2201;
    cmd.desired_power_on = false;
    cmd.issued_at_ms = 100;
    assert(runtime.submit_command(cmd) == core::CoreError::kOk);
    assert(runtime.pending_commands() == 1);

    // Request event only; command stays pending until explicit Zigbee result callback.
    assert(runtime.process_pending() == 1);
    assert(runtime.pending_commands() == 1);

    hal_zigbee_simulate_command_result(cmd.correlation_id, HAL_ZIGBEE_RESULT_SUCCESS);
    assert(runtime.process_pending() == 1);
    assert(runtime.pending_commands() == 0);
    assert(runtime.state().last_command_status == 1);

    hal_wifi_simulate_network_down();
    assert(runtime.process_pending() == 1);
    assert(!runtime.state().network_connected);

    // HAL request contracts remain thin in host mode and return immediate status only.
    assert(hal_zigbee_request_interview(1001U, 0x2201) == HAL_ZIGBEE_STATUS_OK);
    assert(hal_zigbee_request_bind(1002U, 0x2201, 1U, 0x0006U, 1U) == HAL_ZIGBEE_STATUS_OK);
    assert(hal_zigbee_request_configure_reporting(1003U, 0x2201, 1U, 0x0402U, 0x0000U, 5U, 300U, 10U) ==
           HAL_ZIGBEE_STATUS_OK);
    assert(hal_zigbee_request_read_attribute(1004U, 0x2201, 1U, 0x0402U, 0x0000U) == HAL_ZIGBEE_STATUS_OK);

    // Test Device Left scenario
    hal_zigbee_simulate_device_left(0x2201);
    assert(runtime.process_pending() == 1);

    const auto final_state = runtime.state();
    assert(final_state.device_count == 0);
    for (const auto& dev : final_state.devices) {
        assert(dev.short_addr != 0x2201);
    }

    return 0;
}
