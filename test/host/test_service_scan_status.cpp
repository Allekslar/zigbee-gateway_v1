/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "service_runtime_test_access.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    const uint32_t request_id = 7001U;
    assert(runtime.post_network_scan(request_id));
    assert(runtime.is_scan_request_queued(request_id));
    assert(!runtime.is_scan_request_in_progress(request_id));

    service::ServiceRuntimeTestAccess::set_scan_request_in_progress(runtime, request_id);
    assert(runtime.is_scan_request_in_progress(request_id));
    service::ServiceRuntimeTestAccess::clear_scan_request_in_progress(runtime);
    assert(!runtime.is_scan_request_in_progress(request_id));

    // Host path processes scan synchronously in process_pending().
    (void)runtime.process_pending();

    service::ServiceRuntime::NetworkResult result{};
    assert(runtime.take_network_result(request_id, &result));
    assert(result.request_id == request_id);
    assert(result.operation == service::ServiceRuntime::NetworkOperationType::kScan);
    assert(result.status == service::ServiceRuntime::NetworkOperationStatus::kOk);
    assert(!runtime.is_scan_request_queued(request_id));

    const uint32_t failed_request_id = 7002U;
    service::ServiceRuntimeTestAccess::set_scan_request_in_progress(runtime, failed_request_id);
    assert(runtime.is_scan_request_in_progress(failed_request_id));

    service::ServiceRuntime::NetworkResult failed{};
    failed.request_id = failed_request_id;
    failed.operation = service::ServiceRuntime::NetworkOperationType::kScan;
    failed.status = service::ServiceRuntime::NetworkOperationStatus::kHalFailed;
    service::ServiceRuntimeTestAccess::clear_scan_request_in_progress(runtime);
    assert(service::ServiceRuntimeTestAccess::push_network_result(runtime, failed));

    service::ServiceRuntime::NetworkResult failed_out{};
    assert(runtime.take_network_result(failed_request_id, &failed_out));
    assert(failed_out.request_id == failed_request_id);
    assert(failed_out.operation == service::ServiceRuntime::NetworkOperationType::kScan);
    assert(failed_out.status == service::ServiceRuntime::NetworkOperationStatus::kHalFailed);
    assert(!runtime.is_scan_request_in_progress(failed_request_id));

    return 0;
}
