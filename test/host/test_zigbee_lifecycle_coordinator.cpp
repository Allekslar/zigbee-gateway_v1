/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "device_manager.hpp"
#include "effect_executor.hpp"
#include "network_policy_manager.hpp"
#include "service_runtime.hpp"
#include "zigbee_lifecycle_coordinator.hpp"

int main() {
    service::NetworkPolicyManager network_policy_manager{};
    service::DeviceManager device_manager;
    service::ZigbeeLifecycleCoordinator coordinator(network_policy_manager, device_manager);
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    uint16_t seconds_left = 0U;

    assert(!coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 0U);

    coordinator.set_join_window_cache(true, 33U);
    assert(coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 33U);

    coordinator.set_join_window_cache(false, 99U);
    assert(!coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 0U);

    assert(coordinator.handle_join_candidate(runtime, 0x4411U, 1000U));
    assert(runtime.pending_events() == 1U);
    assert(coordinator.handle_join_candidate(runtime, 0x4411U, 1001U));
    assert(runtime.pending_events() == 1U);
    return 0;
}
