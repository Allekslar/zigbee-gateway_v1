/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_nvs.h"
#include "hal_zigbee.h"
#include "service_runtime.hpp"
#include "service_runtime_test_access.hpp"

int main() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("wifi_ssid", "Saved_AP") == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("wifi_password", "saved-pass") == HAL_NVS_STATUS_OK);

    // Emulate startup race: first formation request sees stack not started.
    hal_zigbee_simulate_network_formed(false);
    hal_zigbee_simulate_start_network_formation_status_once(HAL_ZIGBEE_STATUS_NOT_STARTED);

    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.initialize_hal_adapter());
    assert(runtime.has_saved_wifi_credentials());
    assert(runtime.ensure_zigbee_started());
    assert(runtime.zigbee_started());
    assert(!hal_zigbee_is_network_formed());

    const uint32_t base_ms = service::ServiceRuntimeTestAccess::monotonic_now_ms(runtime);
    runtime.tick(base_ms + 1000U);
    assert(hal_zigbee_is_network_formed());

    return 0;
}
