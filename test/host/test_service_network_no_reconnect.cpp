/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "hal_wifi.h"
#include "hal_nvs.h"

/**
 * 
 * 
 * 
 * 
 * 
 */
int main() {
    
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);

    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.initialize_hal_adapter());
    
    
    assert(runtime.state().network_connected == false);
    assert(runtime.has_saved_wifi_credentials() == false);

    
    hal_wifi_simulate_network_down();
    
    
    
    
    runtime.process_pending();

    
    assert(runtime.state().network_connected == false);
    
    
    assert(runtime.zigbee_started() == false);

    return 0;
}