/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "device_id.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

int main() {
    core::DeviceId device_id{};
    assert(core::DeviceId::parse("00124b0001aaaaaa", 16, &device_id));

    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.initialize_hal_adapter());
    assert(runtime.start());
    assert(runtime.start());

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_id = device_id;
    joined.device_short_addr = 0x4411;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1);
    assert(runtime.state().device_count == 1);

    // Persistence round-trips through the explicit versioned schema
    // (persisted_device_state.hpp/persisted_state_store.hpp); verified here
    // through the public restore behavior below rather than by peeking at a
    // specific NVS blob layout, which is exercised in dedicated tests
    // (test_state_persistence_coordinator.cpp, test_persisted_state_store.cpp).

    core::CoreRegistry restored_registry;
    service::EffectExecutor restored_effect_executor;
    service::ServiceRuntime restored_runtime(restored_registry, restored_effect_executor);
    assert(restored_runtime.initialize_hal_adapter());
    assert(restored_runtime.start());
    assert(restored_runtime.state().device_count == 1);

    service::ServiceRuntime::DevicesApiSnapshot devices_snapshot{};
    assert(restored_runtime.build_devices_api_snapshot(1000U, &devices_snapshot));
    assert(devices_snapshot.device_count == 1);
    bool restored_found = false;
    for (std::size_t i = 0; i < devices_snapshot.device_count; ++i) {
        const auto& device = devices_snapshot.devices[i];
        // A device restored from persisted state is sanitized offline (plan
        // Section 9 S3: "online state sanitized to offline on restore") --
        // it must rejoin/report before Core considers it present again.
        if (device.short_addr == 0x4411 && !device.online) {
            restored_found = true;
            break;
        }
    }
    assert(restored_found);

    return 0;
}
