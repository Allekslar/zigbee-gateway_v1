/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_nvs.h"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.initialize_hal_adapter());
    assert(runtime.start());
    assert(runtime.start());

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x4411;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1);
    assert(runtime.state().device_count == 1);

    uint32_t persisted_len = 0U;
    uint8_t persisted_blob[4096]{};
    assert(
        hal_nvs_get_blob("core_state_v1", persisted_blob, static_cast<uint32_t>(sizeof(persisted_blob)), &persisted_len) ==
        HAL_NVS_STATUS_OK);
    assert(persisted_len > 0U);

    core::CoreRegistry restored_registry;
    service::EffectExecutor restored_effect_executor;
    service::ServiceRuntime restored_runtime(restored_registry, restored_effect_executor);
    assert(restored_runtime.initialize_hal_adapter());
    assert(restored_runtime.start());
    assert(restored_runtime.state().device_count == 1);

    service::ServiceRuntime::DevicesApiSnapshot devices_snapshot{};
    assert(restored_runtime.build_devices_api_snapshot(1000U, &devices_snapshot));
    assert(devices_snapshot.state.device_count == 1);
    bool restored_found = false;
    for (const auto& device : devices_snapshot.state.devices) {
        if (device.short_addr == 0x4411 && device.online) {
            restored_found = true;
            break;
        }
    }
    assert(restored_found);

    return 0;
}
