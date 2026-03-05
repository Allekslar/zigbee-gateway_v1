/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "core_state.hpp"

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
    core::CoreState base{};

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x1234;

    const core::CoreReduceResult joined_result = core::core_reduce(base, joined);
    assert(joined_result.next.revision == 1);
    assert(joined_result.next.device_count == 1);
    assert(joined_result.effects.count == 3);
    assert(joined_result.effects.items[0].type == core::CoreEffectType::kPersistState);
    assert(joined_result.effects.items[1].type == core::CoreEffectType::kPublishTelemetry);
    assert(joined_result.effects.items[2].type == core::CoreEffectType::kZigbeeInterview);

    const core::CoreDeviceRecord* joined_device = find_device(joined_result.next, 0x1234);
    assert(joined_device != nullptr);
    assert(joined_device->online);
    assert(!joined_device->power_on);
    assert(joined_device->reporting_state == core::CoreReportingState::kUnknown);
    assert(joined_device->last_report_at_ms == 0);
    assert(!joined_device->stale);
    assert(!joined_device->has_temperature);
    assert(!joined_device->has_battery);
    assert(!joined_device->has_lqi);
    assert(!joined_device->has_rssi);
    assert(joined_device->occupancy_state == core::CoreOccupancyState::kUnknown);
    assert(joined_device->contact_state == core::CoreContactState::kUnknown);

    const core::CoreReduceResult joined_result_repeat = core::core_reduce(base, joined);
    assert(joined_result_repeat.next.revision == joined_result.next.revision);
    assert(joined_result_repeat.next.device_count == joined_result.next.device_count);
    assert(joined_result_repeat.effects.count == joined_result.effects.count);
    assert(joined_result_repeat.effects.items[0].type == joined_result.effects.items[0].type);
    assert(joined_result_repeat.effects.items[1].type == joined_result.effects.items[1].type);

    core::CoreEvent onoff_report{};
    onoff_report.type = core::CoreEventType::kAttributeReported;
    onoff_report.device_short_addr = 0x1234;
    onoff_report.cluster_id = 0x0006;
    onoff_report.attribute_id = 0x0000;
    onoff_report.value_bool = true;

    const core::CoreReduceResult reported = core::core_reduce(joined_result.next, onoff_report);
    assert(reported.next.revision == joined_result.next.revision + 1);
    assert(reported.effects.count == 2);
    joined_device = find_device(reported.next, 0x1234);
    assert(joined_device != nullptr);
    assert(joined_device->power_on);

    core::CoreEvent power_command{};
    power_command.type = core::CoreEventType::kCommandSetDevicePowerRequested;
    power_command.correlation_id = 77;
    power_command.device_short_addr = 0x1234;
    power_command.value_bool = false;

    const core::CoreReduceResult command_result = core::core_reduce(reported.next, power_command);
    assert(command_result.next.revision == reported.next.revision);
    assert(command_result.effects.count == 1);
    assert(command_result.effects.items[0].type == core::CoreEffectType::kSendZigbeeOnOff);
    assert(command_result.effects.items[0].correlation_id == 77);
    assert(command_result.effects.items[0].device_short_addr == 0x1234);
    assert(!command_result.effects.items[0].arg_bool);

    core::CoreEvent timeout_event{};
    timeout_event.type = core::CoreEventType::kCommandResultTimeout;
    timeout_event.correlation_id = 77;
    timeout_event.device_short_addr = 0x1234;

    const core::CoreReduceResult timeout_result = core::core_reduce(command_result.next, timeout_event);
    assert(timeout_result.next.revision == command_result.next.revision + 1);
    assert(timeout_result.next.last_command_status == 2);
    assert(timeout_result.effects.count == 1);
    assert(timeout_result.effects.items[0].type == core::CoreEffectType::kEmitCommandResult);
    assert(timeout_result.effects.items[0].correlation_id == 77);
    assert(!timeout_result.effects.items[0].arg_bool);

    core::CoreEvent interview_completed{};
    interview_completed.type = core::CoreEventType::kDeviceInterviewCompleted;
    interview_completed.device_short_addr = 0x1234;
    const core::CoreReduceResult interview_result = core::core_reduce(timeout_result.next, interview_completed);
    assert(interview_result.next.revision == timeout_result.next.revision + 1);
    assert(interview_result.effects.count == 3);
    assert(interview_result.effects.items[2].type == core::CoreEffectType::kZigbeeBind);
    const core::CoreDeviceRecord* device = find_device(interview_result.next, 0x1234);
    assert(device != nullptr);
    assert(device->reporting_state == core::CoreReportingState::kInterviewCompleted);
    assert(!device->stale);

    core::CoreEvent binding_ready{};
    binding_ready.type = core::CoreEventType::kDeviceBindingReady;
    binding_ready.device_short_addr = 0x1234;
    const core::CoreReduceResult binding_result = core::core_reduce(interview_result.next, binding_ready);
    assert(binding_result.next.revision == interview_result.next.revision + 1);
    assert(binding_result.effects.count == 3);
    assert(binding_result.effects.items[2].type == core::CoreEffectType::kZigbeeConfigureReporting);
    device = find_device(binding_result.next, 0x1234);
    assert(device != nullptr);
    assert(device->reporting_state == core::CoreReportingState::kBindingReady);

    core::CoreEvent reporting_configured{};
    reporting_configured.type = core::CoreEventType::kDeviceReportingConfigured;
    reporting_configured.device_short_addr = 0x1234;
    const core::CoreReduceResult configured_result = core::core_reduce(binding_result.next, reporting_configured);
    assert(configured_result.next.revision == binding_result.next.revision + 1);
    assert(configured_result.effects.count == 3);
    assert(configured_result.effects.items[2].type == core::CoreEffectType::kZigbeeReadAttributes);
    device = find_device(configured_result.next, 0x1234);
    assert(device != nullptr);
    assert(device->reporting_state == core::CoreReportingState::kReportingConfigured);

    core::CoreEvent telemetry_updated{};
    telemetry_updated.type = core::CoreEventType::kDeviceTelemetryUpdated;
    telemetry_updated.device_short_addr = 0x1234;
    telemetry_updated.value_u32 = 4242;
    telemetry_updated.telemetry_kind = core::CoreTelemetryKind::kTemperatureCentiC;
    telemetry_updated.telemetry_i32 = 2150;
    telemetry_updated.telemetry_valid = true;
    const core::CoreReduceResult telemetry_result = core::core_reduce(configured_result.next, telemetry_updated);
    assert(telemetry_result.next.revision == configured_result.next.revision + 1);
    assert(telemetry_result.effects.count == 2);
    device = find_device(telemetry_result.next, 0x1234);
    assert(device != nullptr);
    assert(device->reporting_state == core::CoreReportingState::kReportingActive);
    assert(!device->stale);
    assert(device->last_report_at_ms == 4242);
    assert(device->has_temperature);
    assert(device->temperature_centi_c == 2150);

    core::CoreEvent telemetry_invalid = telemetry_updated;
    telemetry_invalid.value_u32 = 4343;
    telemetry_invalid.telemetry_valid = false;
    telemetry_invalid.telemetry_i32 = 0;
    const core::CoreReduceResult telemetry_invalid_result = core::core_reduce(telemetry_result.next, telemetry_invalid);
    assert(telemetry_invalid_result.next.revision == telemetry_result.next.revision + 1);
    device = find_device(telemetry_invalid_result.next, 0x1234);
    assert(device != nullptr);
    assert(!device->has_temperature);

    core::CoreEvent occupancy_event{};
    occupancy_event.type = core::CoreEventType::kDeviceTelemetryUpdated;
    occupancy_event.device_short_addr = 0x1234;
    occupancy_event.value_u32 = 4444;
    occupancy_event.telemetry_kind = core::CoreTelemetryKind::kOccupancy;
    occupancy_event.telemetry_i32 = 1;
    occupancy_event.telemetry_valid = true;
    const core::CoreReduceResult occupancy_result = core::core_reduce(telemetry_invalid_result.next, occupancy_event);
    assert(occupancy_result.next.revision == telemetry_invalid_result.next.revision + 1);
    device = find_device(occupancy_result.next, 0x1234);
    assert(device != nullptr);
    assert(device->occupancy_state == core::CoreOccupancyState::kOccupied);

    core::CoreEvent stale_event{};
    stale_event.type = core::CoreEventType::kDeviceStale;
    stale_event.device_short_addr = 0x1234;
    const core::CoreReduceResult stale_result = core::core_reduce(occupancy_result.next, stale_event);
    assert(stale_result.next.revision == occupancy_result.next.revision + 1);
    assert(stale_result.effects.count == 2);
    device = find_device(stale_result.next, 0x1234);
    assert(device != nullptr);
    assert(device->reporting_state == core::CoreReportingState::kStale);
    assert(device->stale);

    return 0;
}
