/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// End-to-end proof of the S4 slice of FD-17: ServiceRuntime resolves a
// canonical GatewayId from the HAL-owned factory base MAC exactly once,
// two distinct base MACs yield two distinct GatewayIds, and the same base
// MAC deterministically yields the same GatewayId across a fresh
// ServiceRuntime instance (simulating reboot: the underlying eFuse value
// does not change).

#include <cassert>
#include <cstring>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_identity_test.h"
#include "service_runtime.hpp"

int main() {
    const uint8_t mac_a[HAL_IDENTITY_BASE_MAC_LEN] = {0x00, 0x12, 0x4b, 0x11, 0x22, 0x33};
    hal_identity_set_mock_base_mac(mac_a);

    core::CoreRegistry registry_a;
    service::EffectExecutor effect_executor_a;
    service::ServiceRuntime runtime_a(registry_a, effect_executor_a);

    const common::GatewayId gateway_id_a = runtime_a.gateway_id();
    assert(gateway_id_a.valid());
    char formatted_a[common::GatewayId::kHexLength] = {};
    assert(gateway_id_a.format(formatted_a, sizeof(formatted_a)));
    assert(std::memcmp(formatted_a, "00124b112233", common::GatewayId::kHexLength) == 0);

    // Reboot simulation: same base MAC, fresh ServiceRuntime -> same GatewayId.
    core::CoreRegistry registry_a_reboot;
    service::EffectExecutor effect_executor_a_reboot;
    service::ServiceRuntime runtime_a_reboot(registry_a_reboot, effect_executor_a_reboot);
    assert(runtime_a_reboot.gateway_id() == gateway_id_a);

    // Two hardware units (distinct factory base MACs) -> distinct GatewayIds.
    const uint8_t mac_b[HAL_IDENTITY_BASE_MAC_LEN] = {0x00, 0x12, 0x4b, 0x44, 0x55, 0x66};
    hal_identity_set_mock_base_mac(mac_b);

    core::CoreRegistry registry_b;
    service::EffectExecutor effect_executor_b;
    service::ServiceRuntime runtime_b(registry_b, effect_executor_b);
    const common::GatewayId gateway_id_b = runtime_b.gateway_id();
    assert(gateway_id_b.valid());
    assert(gateway_id_b != gateway_id_a);

    // Accessible through the ServiceRuntimeApi interface, since every
    // production call site (Web/MQTT/HA, once wired in a later S4 pass)
    // holds the interface, not the concrete class.
    service::ServiceRuntimeApi& api = runtime_b;
    assert(api.gateway_id() == gateway_id_b);

    hal_identity_reset_mock_base_mac();
    return 0;
}
