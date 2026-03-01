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
 */
int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    const uint32_t request_id = 12345U;

    
    assert(!runtime.post_network_scan(0)); 

    
    assert(runtime.post_network_scan(request_id));
    
    
    assert(runtime.is_scan_request_queued(request_id));

    
    
    runtime.process_pending();

    
    assert(!runtime.is_scan_request_queued(request_id));

    
    service::ServiceRuntime::NetworkResult result{};
    assert(runtime.take_network_result(request_id, &result));

    
    assert(result.request_id == request_id);
    assert(result.operation == service::ServiceRuntime::NetworkOperationType::kScan);
    assert(result.status == service::ServiceRuntime::NetworkOperationStatus::kOk);
    
    
    assert(result.scan_count == 3);
    
    
    assert(std::strcmp(result.scan_records[0].ssid, "HomeWiFi") == 0);
    assert(result.scan_records[0].rssi == -45);
    assert(result.scan_records[0].is_open == false);

    
    assert(std::strcmp(result.scan_records[2].ssid, "Guest") == 0);
    assert(result.scan_records[2].is_open == true);

    
    service::ServiceRuntime::NetworkResult second_attempt{};
    assert(!runtime.take_network_result(request_id, &second_attempt));

    return 0;
}