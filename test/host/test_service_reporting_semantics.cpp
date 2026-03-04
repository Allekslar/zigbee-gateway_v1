/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_zigbee.h"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);

    const uint8_t valid_payload[2] = {0x66U, 0x08U};  // 0x0866 => 2150 (21.50 C)
    hal_zigbee_raw_attribute_report_t valid_report{};
    valid_report.short_addr = 0x2201U;
    valid_report.endpoint = 1U;
    valid_report.cluster_id = 0x0402U;
    valid_report.attribute_id = 0x0000U;
    valid_report.payload = valid_payload;
    valid_report.payload_len = 2U;
    assert(runtime.post_zigbee_attribute_report_raw(valid_report));
    assert(runtime.process_pending() == 1U);

    core::CoreState state_after_valid = runtime.state();
    const core::CoreDeviceRecord* device = nullptr;
    for (const auto& record : state_after_valid.devices) {
        if (record.short_addr == 0x2201U && record.online) {
            device = &record;
            break;
        }
    }
    assert(device != nullptr);
    assert(device->has_temperature);
    assert(device->temperature_centi_c == 2150);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    assert(device->last_report_at_ms != 0U);
    const uint32_t last_report_after_valid = device->last_report_at_ms;

    const uint8_t invalid_payload[2] = {0x00U, 0x80U};  // invalid marker 0x8000
    hal_zigbee_raw_attribute_report_t invalid_report{};
    invalid_report.short_addr = 0x2201U;
    invalid_report.endpoint = 1U;
    invalid_report.cluster_id = 0x0402U;
    invalid_report.attribute_id = 0x0000U;
    invalid_report.payload = invalid_payload;
    invalid_report.payload_len = 2U;
    assert(runtime.post_zigbee_attribute_report_raw(invalid_report));
    assert(runtime.process_pending() == 1U);

    core::CoreState state_after_invalid = runtime.state();
    device = nullptr;
    for (const auto& record : state_after_invalid.devices) {
        if (record.short_addr == 0x2201U && record.online) {
            device = &record;
            break;
        }
    }
    assert(device != nullptr);
    assert(!device->has_temperature);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    assert(device->last_report_at_ms >= last_report_after_valid);

    return 0;
}

