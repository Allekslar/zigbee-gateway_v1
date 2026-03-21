/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "device_manager.hpp"
#include "effect_executor.hpp"
#include "network_manager.hpp"
#include "network_policy_manager.hpp"
#include "service_runtime.hpp"
#include "service_runtime_test_access.hpp"
#include "zigbee_lifecycle_coordinator.hpp"

int main() {
    service::NetworkPolicyManager network_policy_manager{};
    service::DeviceManager device_manager;
    service::ZigbeeLifecycleCoordinator coordinator(network_policy_manager, device_manager);
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());
    uint16_t seconds_left = 0U;

    assert(!coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 0U);

    coordinator.set_join_window_cache(true, 33U);
    assert(coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 33U);

    coordinator.set_join_window_cache(false, 99U);
    assert(!coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 0U);

    const std::size_t initial_pending_events = runtime.pending_events();
    assert(coordinator.handle_join_candidate(runtime, 0x4411U, 1000U));
    assert(runtime.pending_events() == (initial_pending_events + 1U));
    assert(coordinator.handle_join_candidate(runtime, 0x4411U, 1001U));
    assert(runtime.pending_events() == (initial_pending_events + 1U));

    runtime.mark_wifi_credentials_available();
    assert(runtime.ensure_zigbee_started());

    service::NetworkRequest remove_request{};
    remove_request.request_id = 7U;
    remove_request.operation = service::NetworkOperationType::kRemoveDevice;
    remove_request.device_short_addr = 0x4411U;
    remove_request.force_remove = true;
    remove_request.force_remove_timeout_ms = 1000U;

    service::NetworkResult remove_result{};
    assert(coordinator.handle_remove_device(runtime, remove_request, &remove_result));
    assert(remove_result.status == service::NetworkOperationStatus::kOk);
    assert(remove_result.device_short_addr == 0x4411U);

    const uint32_t now_ms = service::ServiceRuntimeTestAccess::monotonic_now_ms(runtime);
    assert(coordinator.process_force_remove_timeouts(runtime, now_ms + 1100U) == 1U);
    assert(runtime.pending_events() == (initial_pending_events + 2U));
    return 0;
}
