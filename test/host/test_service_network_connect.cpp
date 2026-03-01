/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

/**
 * 
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

    const uint32_t request_id = 999U;
    const char* ssid = "Test_AP";
    const char* pass = "password123";

    
    assert(runtime.post_network_connect(request_id, ssid, pass, true));

    
    
    
    
    runtime.process_pending();

    
    assert(runtime.state().network_connected == true);

    
    
    runtime.tick(1000);

    
    service::ServiceRuntime::NetworkResult result{};
    assert(runtime.take_network_result(request_id, &result));

    assert(result.request_id == request_id);
    assert(result.operation == service::ServiceRuntime::NetworkOperationType::kConnect);
    assert(result.status == service::ServiceRuntime::NetworkOperationStatus::kOk);
    assert(result.saved == true);
    assert(std::strcmp(result.ssid, ssid) == 0);

    
    assert(!runtime.take_network_result(request_id, &result));

    return 0;
}