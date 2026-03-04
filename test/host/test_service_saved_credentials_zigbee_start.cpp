/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_nvs.h"
#include "service_runtime.hpp"

int main() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("wifi_ssid", "Saved_AP") == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("wifi_password", "saved-pass") == HAL_NVS_STATUS_OK);

    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.initialize_hal_adapter());
    assert(runtime.has_saved_wifi_credentials());
    assert(runtime.zigbee_started());
    assert(runtime.ensure_zigbee_started());

    return 0;
}
