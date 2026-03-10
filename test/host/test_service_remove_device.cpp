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
    (void)runtime.process_pending();
    assert(runtime.state().device_count == 1U);

    service::ServiceRuntime::NetworkResult result{};

    // Without Zigbee start permission, non-force remove must fail cleanly.
    assert(runtime.post_remove_device(101U, 0x2201U, false, 0U));
    (void)runtime.process_pending();
    assert(runtime.take_network_result(101U, &result));
    assert(result.operation == service::NetworkOperationType::kRemoveDevice);
    assert(result.status == service::NetworkOperationStatus::kHalFailed);
    assert(runtime.state().device_count == 1U);

    // Once Zigbee is allowed to start, remove request should be accepted and
    // actual state removal should still wait for the leave indication.
    runtime.mark_wifi_credentials_available();
    assert(runtime.ensure_zigbee_started());

    assert(runtime.post_remove_device(102U, 0x2201U, false, 0U));
    (void)runtime.process_pending();
    assert(runtime.take_network_result(102U, &result));
    assert(result.operation == service::NetworkOperationType::kRemoveDevice);
    assert(result.status == service::NetworkOperationStatus::kOk);
    assert(result.device_short_addr == 0x2201U);
    assert(runtime.state().device_count == 1U);

    hal_zigbee_simulate_device_left(0x2201U);
    assert(runtime.process_pending() > 0U);
    assert(runtime.state().device_count == 0U);

    return 0;
}
