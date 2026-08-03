/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    // Default-constructed capabilities are all-false: an unset capability must
    // never be silently assumed available (S1 truthfulness requirement).
    const service::RuntimeCapabilities default_caps = runtime.capabilities();
    assert(!default_caps.zigbee_available);
    assert(!default_caps.mqtt_available);
    assert(!default_caps.matter_target_available);
    assert(!default_caps.ota_available);
    assert(!default_caps.rcp_update_available);

    // set_capabilities()/capabilities() is a plain injected projection: the
    // composition root computes real values (Kconfig/HAL) and the service
    // layer only stores/returns them, with no ESP-IDF dependency of its own.
    service::RuntimeCapabilities injected{};
    injected.zigbee_available = true;
    injected.mqtt_available = false;
    injected.matter_target_available = true;
    injected.ota_available = true;
    injected.rcp_update_available = false;
    runtime.set_capabilities(injected);

    const service::RuntimeCapabilities read_back = runtime.capabilities();
    assert(read_back.zigbee_available);
    assert(!read_back.mqtt_available);
    assert(read_back.matter_target_available);
    assert(read_back.ota_available);
    assert(!read_back.rcp_update_available);

    // A later call fully replaces the projection (no field-by-field merge).
    service::RuntimeCapabilities all_false{};
    runtime.set_capabilities(all_false);
    const service::RuntimeCapabilities cleared = runtime.capabilities();
    assert(!cleared.zigbee_available);
    assert(!cleared.matter_target_available);
    assert(!cleared.ota_available);

    return 0;
}
