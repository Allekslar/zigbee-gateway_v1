/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "hal_wifi.h"

/**
 * 
 * 
 * 
 * 
 */
int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.initialize_hal_adapter());

    
    const char* ssid = "Reconnect_AP";
    assert(runtime.post_network_connect(1, ssid, "password", true));
    
    
    runtime.process_pending();
    runtime.tick(1000);
    assert(runtime.state().network_connected == true);

    
    
    hal_wifi_simulate_network_down();
    
    
    
    
    
    runtime.process_pending();

    
    
    assert(runtime.state().network_connected == true);
    
    
    assert(runtime.zigbee_started() == true);

    return 0;
}