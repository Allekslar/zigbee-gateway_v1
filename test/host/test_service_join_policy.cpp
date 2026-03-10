/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    hal_zigbee_simulate_device_joined(0x2201);
    hal_zigbee_simulate_device_joined(0x2201);
    hal_zigbee_simulate_device_joined(0x2201);

    assert(runtime.process_pending() == 1);
    assert(runtime.state().device_count == 1);

    hal_zigbee_simulate_device_joined(0x2201);
    assert(runtime.process_pending() == 0);
    assert(runtime.state().device_count == 1);

    hal_zigbee_simulate_device_joined(0x2202);
    assert(runtime.process_pending() == 1);
    assert(runtime.state().device_count == 2);

    return 0;
}
