/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// End-to-end proof that a resolved EUI-64 supplied at the HAL boundary flows
// all the way through hal_event_adapter.cpp -> DeviceLocatorRegistry ->
// Core, closing the Service-layer wiring gap left open at the end of S2's
// first pass (see docs/architecture/DEVICE_IDENTITY.md).

#include <array>
#include <cassert>
#include <cstddef>

#include "core_registry.hpp"
#include "core_state.hpp"
#include "device_id.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "service_runtime.hpp"

namespace {

const core::CoreDeviceRecord* find_by_device_id(const core::CoreState& state, const core::DeviceId& id) {
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        if (state.devices[i].device_id == id) {
            return &state.devices[i];
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);
    assert(runtime.initialize_hal_adapter());

    const std::array<uint8_t, HAL_ZIGBEE_IEEE_ADDR_LEN> ieee_a = {
        0x00, 0x12, 0x4b, 0x00, 0x01, 0xaa, 0xaa, 0xaa};
    core::DeviceId device_a{};
    assert(core::DeviceId::parse("00124b0001aaaaaa", 16, &device_a));

    // --- Join with a resolved identity flows through to Core ---
    hal_zigbee_simulate_device_joined_with_identity(0x3301U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    core::CoreState state = runtime.state();
    const core::CoreDeviceRecord* rec = find_by_device_id(state, device_a);
    assert(rec != nullptr);
    assert(rec->short_addr == 0x3301U);
    assert(rec->online);

    // --- The locator registry reflects the same mapping the reducer used ---
    service::DeviceLocatorEntry locator_entry{};
    assert(runtime.device_locator_registry().find_by_device_id(device_a, &locator_entry));
    assert(locator_entry.short_addr == 0x3301U);
    assert(runtime.device_locator_registry().find_by_short_addr(0x3301U, &locator_entry));
    assert(locator_entry.device_id == device_a);

    // --- Interview/bind/reporting-configured results resolve device_id from
    // the registry via short_addr and reach the SAME Core record (not a
    // short_addr-only shadow record). ---
    hal_zigbee_notify_interview_result(9001U, 0x3301U, HAL_ZIGBEE_RESULT_SUCCESS);
    hal_zigbee_notify_bind_result(9002U, 0x3301U, HAL_ZIGBEE_RESULT_SUCCESS);
    hal_zigbee_notify_configure_reporting_result(9003U, 0x3301U, HAL_ZIGBEE_RESULT_SUCCESS);
    assert(runtime.process_pending() == 3U);

    state = runtime.state();
    assert(state.device_count == 1U);  // still exactly one device, not four shadow records
    rec = find_by_device_id(state, device_a);
    assert(rec != nullptr);
    assert(rec->reporting_state == core::CoreReportingState::kReportingConfigured);

    // --- INV-ID-01: short-address remap for the same resolved identity
    // updates the SAME record and the SAME locator-registry entry. ---
    hal_zigbee_simulate_device_joined_with_identity(0x3302U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    state = runtime.state();
    assert(state.device_count == 1U);
    rec = find_by_device_id(state, device_a);
    assert(rec != nullptr);
    assert(rec->short_addr == 0x3302U);
    assert(rec->reporting_state == core::CoreReportingState::kReportingConfigured);  // not reset

    assert(!runtime.device_locator_registry().find_by_short_addr(0x3301U, &locator_entry));
    assert(runtime.device_locator_registry().find_by_short_addr(0x3302U, &locator_entry));
    assert(locator_entry.device_id == device_a);

    // --- INV-ID-02/03: a different resolved identity joining creates an
    // independent record; it does not inherit device_a's reporting_state.
    // (A genuinely fresh short_addr is used here rather than reusing the
    // 0x3301 device_a just vacated, to avoid DeviceManager's short_addr+time
    // -windowed join-candidate dedup -- an orthogonal policy, not part of
    // this identity-wiring test, that is keyed by short_addr regardless of
    // device identity and would otherwise suppress this candidate since it
    // runs within the same dedup window as device_a's first join above.
    // DeviceLocatorRegistry's own displacement semantics for an EXACTLY
    // reused short_addr are covered synchronously, without this real-time
    // dependency, in test_device_locator_registry.cpp and
    // test_core_reducer_device_id.cpp.)
    const std::array<uint8_t, HAL_ZIGBEE_IEEE_ADDR_LEN> ieee_b = {
        0x00, 0x12, 0x4b, 0x00, 0x01, 0xbb, 0xbb, 0xbb};
    core::DeviceId device_b{};
    assert(core::DeviceId::parse("00124b0001bbbbbb", 16, &device_b));

    hal_zigbee_simulate_device_joined_with_identity(0x3303U, ieee_b.data());
    assert(runtime.process_pending() >= 1U);

    state = runtime.state();
    assert(state.device_count == 2U);
    const core::CoreDeviceRecord* rec_b = find_by_device_id(state, device_b);
    assert(rec_b != nullptr);
    assert(rec_b->short_addr == 0x3303U);
    assert(rec_b->reporting_state == core::CoreReportingState::kUnknown);  // fresh, nothing inherited

    // --- Leave marks the locator offline and removes the Core record ---
    hal_zigbee_simulate_device_left_with_identity(0x3302U, ieee_a.data());
    assert(runtime.process_pending() >= 1U);

    state = runtime.state();
    assert(find_by_device_id(state, device_a) == nullptr);
    assert(state.device_count == 1U);
    // The locator registry keeps the record (marked offline, per the plan's
    // "displace without erasing" semantics) even though Core removed its
    // device record on kDeviceLeft; the registry and Core are deliberately
    // separate stores with different lifecycles.
    assert(runtime.device_locator_registry().find_by_device_id(device_a, &locator_entry));
    assert(locator_entry.status == service::DeviceLocatorStatus::kOffline);
    assert(!runtime.device_locator_registry().find_by_short_addr(0x3302U, &locator_entry));

    return 0;
}
