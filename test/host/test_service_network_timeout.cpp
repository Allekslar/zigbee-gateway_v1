/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

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

    const uint32_t request_id = 777U;
    const uint32_t start_time_ms = 1000U;

    
    assert(runtime.post_network_connect(request_id, "Slow_AP", "password", false));
    
    runtime.tick(start_time_ms);
    
    
    runtime.process_pending();

    
    runtime.tick(start_time_ms + 5000U);
    service::ServiceRuntime::NetworkResult result{};
    assert(!runtime.take_network_result(request_id, &result));

    
    
    
    
    runtime.tick(start_time_ms + 11500U);
    assert(!runtime.take_network_result(request_id, &result));

    
    
    runtime.tick(start_time_ms + 13000U);

    
    assert(runtime.take_network_result(request_id, &result));
    assert(result.request_id == request_id);
    assert(result.operation == service::ServiceRuntime::NetworkOperationType::kConnect);
    assert(result.status == service::ServiceRuntime::NetworkOperationStatus::kHalFailed);

    
    assert(runtime.state().network_connected == false);

    return 0;
}