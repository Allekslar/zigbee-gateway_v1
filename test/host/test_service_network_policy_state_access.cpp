/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "network_policy_manager.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    service::NetworkPolicyManager manager;

    assert(runtime.initialize_hal_adapter());

    service::ServiceRuntime::NetworkResult result{};

    manager.arm_pending_sta_connect(10U, true, "StillPending", 5000U);
    assert(manager.process_pending_sta_connect(runtime, false, 1000U) == 0U);
    assert(!runtime.take_network_result(10U, &result));

    manager.arm_pending_sta_connect(11U, false, "TimedOut", 1500U);
    assert(manager.process_pending_sta_connect(runtime, false, 2000U) == 1U);
    assert(runtime.take_network_result(11U, &result));
    assert(result.request_id == 11U);
    assert(result.operation == service::ServiceRuntime::NetworkOperationType::kConnect);
    assert(result.status == service::ServiceRuntime::NetworkOperationStatus::kHalFailed);
    assert(result.saved == false);
    assert(std::strcmp(result.ssid, "TimedOut") == 0);

    manager.arm_pending_sta_connect(12U, true, "ConnectedNow", 6000U);
    assert(manager.process_pending_sta_connect(runtime, true, 2500U) == 1U);
    assert(runtime.take_network_result(12U, &result));
    assert(result.request_id == 12U);
    assert(result.operation == service::ServiceRuntime::NetworkOperationType::kConnect);
    assert(result.status == service::ServiceRuntime::NetworkOperationStatus::kOk);
    assert(result.saved == true);
    assert(std::strcmp(result.ssid, "ConnectedNow") == 0);

    return 0;
}
