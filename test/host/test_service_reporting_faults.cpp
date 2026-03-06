/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "reporting_manager.hpp"
#include "service_runtime.hpp"

namespace {

const core::CoreDeviceRecord* find_online_device(const core::CoreState& state, uint16_t short_addr) {
    for (const auto& device : state.devices) {
        if (device.short_addr == short_addr && device.online) {
            return &device;
        }
    }
    return nullptr;
}

core::CoreEvent make_event(core::CoreEventType type, uint16_t short_addr) {
    core::CoreEvent event{};
    event.type = type;
    event.device_short_addr = short_addr;
    return event;
}

}  // namespace

int main() {
    // Fault 1: malformed payload must be rejected and must not corrupt state.
    {
        core::CoreRegistry registry;
        service::EffectExecutor effect_executor;
        service::ServiceRuntime runtime(registry, effect_executor);

        core::CoreEvent joined{};
        joined.type = core::CoreEventType::kDeviceJoined;
        joined.device_short_addr = 0x4101U;
        assert(runtime.post_event(joined));
        assert(runtime.process_pending() == 1U);

        const core::CoreState before = runtime.state();
        const core::CoreDeviceRecord* before_device = find_online_device(before, 0x4101U);
        assert(before_device != nullptr);
        assert(!before_device->has_temperature);

        hal_zigbee_raw_attribute_report_t malformed{};
        malformed.short_addr = 0x4101U;
        malformed.endpoint = 1U;
        malformed.cluster_id = 0x0402U;
        malformed.attribute_id = 0x0000U;
        malformed.payload = nullptr;   // malformed for temperature (expects 2 bytes)
        malformed.payload_len = 1U;    // malformed length
        assert(!runtime.post_zigbee_attribute_report_raw(malformed));
        assert(runtime.process_pending() == 0U);

        const core::CoreState after = runtime.state();
        const core::CoreDeviceRecord* after_device = find_online_device(after, 0x4101U);
        assert(after_device != nullptr);
        assert(!after_device->has_temperature);
        assert(after_device->last_report_at_ms == before_device->last_report_at_ms);
        assert(after_device->reporting_state == before_device->reporting_state);
    }

    // Fault 2: out-of-order telemetry before join must be ignored (no corruption).
    {
        core::CoreRegistry registry;
        service::EffectExecutor effect_executor;
        service::ServiceRuntime runtime(registry, effect_executor);

        const uint8_t temp_payload[2] = {0x66U, 0x08U};  // 21.50 C
        hal_zigbee_raw_attribute_report_t early{};
        early.short_addr = 0x4102U;
        early.endpoint = 1U;
        early.cluster_id = 0x0402U;
        early.attribute_id = 0x0000U;
        early.payload = temp_payload;
        early.payload_len = 2U;
        assert(runtime.post_zigbee_attribute_report_raw(early));
        assert(runtime.process_pending() == 1U);
        assert(runtime.state().device_count == 0U);
        assert(find_online_device(runtime.state(), 0x4102U) == nullptr);

        core::CoreEvent joined{};
        joined.type = core::CoreEventType::kDeviceJoined;
        joined.device_short_addr = 0x4102U;
        assert(runtime.post_event(joined));
        assert(runtime.process_pending() == 1U);

        assert(runtime.post_zigbee_attribute_report_raw(early));
        assert(runtime.process_pending() == 1U);
        const core::CoreDeviceRecord* device = find_online_device(runtime.state(), 0x4102U);
        assert(device != nullptr);
        assert(device->has_temperature);
        assert(device->temperature_centi_c == 2150);
    }

    // Fault 3: duplicate report must not corrupt state.
    {
        core::CoreRegistry registry;
        service::EffectExecutor effect_executor;
        service::ServiceRuntime runtime(registry, effect_executor);

        core::CoreEvent joined{};
        joined.type = core::CoreEventType::kDeviceJoined;
        joined.device_short_addr = 0x4103U;
        assert(runtime.post_event(joined));
        assert(runtime.process_pending() == 1U);

        const uint8_t temp_payload[2] = {0x66U, 0x08U};  // 21.50 C
        hal_zigbee_raw_attribute_report_t report{};
        report.short_addr = 0x4103U;
        report.endpoint = 1U;
        report.cluster_id = 0x0402U;
        report.attribute_id = 0x0000U;
        report.payload = temp_payload;
        report.payload_len = 2U;

        assert(runtime.post_zigbee_attribute_report_raw(report));
        assert(runtime.process_pending() == 1U);
        assert(runtime.post_zigbee_attribute_report_raw(report));
        assert(runtime.process_pending() == 1U);

        const core::CoreDeviceRecord* device = find_online_device(runtime.state(), 0x4103U);
        assert(device != nullptr);
        assert(device->has_temperature);
        assert(device->temperature_centi_c == 2150);
        assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    }

    // Fault 4: timeout is tracked and degraded state is visible.
    {
        service::ReportingManager manager{};
        (void)manager.handle_event(make_event(core::CoreEventType::kDeviceJoined, 0x4104U));
        (void)manager.handle_event(make_event(core::CoreEventType::kDeviceInterviewCompleted, 0x4104U));

        const service::ReportingManager::RuntimeActions actions = manager.report_operation_failure(
            0x4104U,
            service::ReportingManager::RetryTarget::kBind,
            service::ReportingManager::FailureReason::kTimeout,
            1000U);
        assert(!actions.mark_degraded);

        service::ReportingManager::RetryStatus retry{};
        assert(manager.get_retry_status(0x4104U, &retry));
        assert(retry.reason == service::ReportingManager::FailureReason::kTimeout);
        assert(retry.pending);
    }

    return 0;
}

