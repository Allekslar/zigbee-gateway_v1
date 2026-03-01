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
    assert(hal_nvs_set_str("wifi_ssid", "Retry_AP") == HAL_NVS_STATUS_OK);

    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    
    uint32_t current_time = 1000;
    runtime.tick(current_time);
    assert(runtime.initialize_hal_adapter());

    
    hal_wifi_simulate_connect_failure(true);

    
    for (int i = 1; i <= 6; ++i) {
        runtime.tick(current_time);
        hal_wifi_simulate_network_down();
        runtime.process_pending();
        
        if (i <= 5) {
            
            assert(runtime.stats().autoconnect_failures == static_cast<uint32_t>(i));
            current_time += 20000; 
        } else {
            
            assert(runtime.stats().autoconnect_failures == 5);
        }
    }

    
    hal_wifi_simulate_connect_failure(false); 
    assert(runtime.post_network_connect(100, "New_AP", "pass", true));
    runtime.process_pending();
    runtime.tick(1000);
    
    assert(runtime.state().network_connected == true);

    
    hal_wifi_simulate_network_down();
    runtime.process_pending();
    
    assert(runtime.state().network_connected == true);

    return 0;
}