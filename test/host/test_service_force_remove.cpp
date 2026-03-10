/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "service_runtime.hpp"

/**
 * 
 * 
 * 
 * 
 */
int main() {
    core::CoreRegistry registry;
    service::EffectExecutor executor;
    service::ServiceRuntime runtime(registry, executor);
    assert(runtime.initialize_hal_adapter());

    uint32_t current_time = 1000;
    runtime.tick(current_time);

    
    hal_zigbee_simulate_device_joined(0xAAAA);
    runtime.process_pending();
    assert(runtime.state().device_count == 1);

    
    uint32_t req_id = 500;
    assert(runtime.post_remove_device(req_id, 0xAAAA, true, 1000));
    runtime.process_pending();

    
    assert(runtime.state().device_count == 1);

    
    runtime.tick(current_time + 500);
    runtime.process_pending();
    assert(runtime.state().device_count == 1);

    
    runtime.tick(current_time + 1100);
    runtime.process_pending();

    
    assert(runtime.state().device_count == 0);
    assert(runtime.state().devices[0].short_addr == 0xFFFF);

    return 0;
}
