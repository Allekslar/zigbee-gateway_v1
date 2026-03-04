/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    // Full async lifecycle in host mode: join -> interview -> bind -> configure reporting.
    hal_zigbee_simulate_device_joined(0x2201);
    assert(runtime.process_pending() == 1);

    hal_zigbee_simulate_interview_completed(1001U, 0x2201);
    assert(runtime.process_pending() == 1);

    hal_zigbee_simulate_bind_result(1002U, 0x2201, HAL_ZIGBEE_RESULT_SUCCESS);
    assert(runtime.process_pending() == 1);

    hal_zigbee_simulate_reporting_config_result(1003U, 0x2201, HAL_ZIGBEE_RESULT_SUCCESS);
    assert(runtime.process_pending() == 1);

    bool found = false;
    const auto state = runtime.state();
    for (const auto& device : state.devices) {
        if (device.short_addr != 0x2201 || !device.online) {
            continue;
        }
        assert(device.reporting_state == core::CoreReportingState::kReportingConfigured);
        found = true;
    }
    assert(found);

    // Failed lifecycle callback must not mutate state.
    hal_zigbee_simulate_bind_result(1004U, 0x2201, HAL_ZIGBEE_RESULT_FAILED);
    assert(runtime.process_pending() == 0);
    const auto after_failed = runtime.state();
    bool still_configured = false;
    for (const auto& device : after_failed.devices) {
        if (device.short_addr != 0x2201 || !device.online) {
            continue;
        }
        assert(device.reporting_state == core::CoreReportingState::kReportingConfigured);
        still_configured = true;
    }
    assert(still_configured);

    return 0;
}
