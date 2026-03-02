/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "core_state.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

namespace {

const core::CoreDeviceRecord* find_device(const core::CoreState& state, uint16_t short_addr) {
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        if (state.devices[i].short_addr == short_addr) {
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

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x7711;
    assert(runtime.post_event(joined));

    core::CoreEvent reporting_configured{};
    reporting_configured.type = core::CoreEventType::kDeviceReportingConfigured;
    reporting_configured.device_short_addr = 0x7711;
    assert(runtime.post_event(reporting_configured));

    core::CoreEvent telemetry{};
    telemetry.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry.device_short_addr = 0x7711;
    telemetry.value_u32 = 1000U;
    assert(runtime.post_event(telemetry));

    assert(runtime.process_pending() == 3);
    const core::CoreDeviceRecord* device = find_device(runtime.state(), 0x7711);
    assert(device != nullptr);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    assert(!device->stale);

    // Silence window is 15000ms, first stale point is at 16000ms.
    assert(runtime.tick(15999U) == 0);
    assert(runtime.process_pending() == 0);
    device = find_device(runtime.state(), 0x7711);
    assert(device != nullptr);
    assert(!device->stale);

    assert(runtime.tick(16000U) == 1);
    assert(runtime.process_pending() == 1);
    device = find_device(runtime.state(), 0x7711);
    assert(device != nullptr);
    assert(device->stale);
    assert(device->reporting_state == core::CoreReportingState::kStale);

    core::CoreEvent recovered_telemetry{};
    recovered_telemetry.type = core::CoreEventType::kDeviceTelemetryUpdated;
    recovered_telemetry.device_short_addr = 0x7711;
    recovered_telemetry.value_u32 = 17000U;
    assert(runtime.post_event(recovered_telemetry));
    assert(runtime.process_pending() == 1);
    device = find_device(runtime.state(), 0x7711);
    assert(device != nullptr);
    assert(!device->stale);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);

    return 0;
}
