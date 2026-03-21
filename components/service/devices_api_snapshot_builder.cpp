/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "devices_api_snapshot_builder.hpp"

#include "core_state.hpp"

namespace service {

namespace {

DeviceReportingState to_device_reporting_state(core::CoreReportingState state) noexcept {
    switch (state) {
        case core::CoreReportingState::kInterviewCompleted:
            return DeviceReportingState::kInterviewCompleted;
        case core::CoreReportingState::kBindingReady:
            return DeviceReportingState::kBindingReady;
        case core::CoreReportingState::kReportingConfigured:
            return DeviceReportingState::kReportingConfigured;
        case core::CoreReportingState::kReportingActive:
            return DeviceReportingState::kReportingActive;
        case core::CoreReportingState::kStale:
            return DeviceReportingState::kStale;
        case core::CoreReportingState::kUnknown:
        default:
            return DeviceReportingState::kUnknown;
    }
}

DeviceOccupancyState to_device_occupancy_state(core::CoreOccupancyState state) noexcept {
    switch (state) {
        case core::CoreOccupancyState::kNotOccupied:
            return DeviceOccupancyState::kNotOccupied;
        case core::CoreOccupancyState::kOccupied:
            return DeviceOccupancyState::kOccupied;
        case core::CoreOccupancyState::kUnknown:
        default:
            return DeviceOccupancyState::kUnknown;
    }
}

DeviceContactState to_device_contact_state(core::CoreContactState state) noexcept {
    switch (state) {
        case core::CoreContactState::kClosed:
            return DeviceContactState::kClosed;
        case core::CoreContactState::kOpen:
            return DeviceContactState::kOpen;
        case core::CoreContactState::kUnknown:
        default:
            return DeviceContactState::kUnknown;
    }
}

}  // namespace

bool DevicesApiSnapshotBuilder::build(
    const core::CoreState& state,
    const DeviceRuntimeSnapshot& runtime_snapshot,
    DevicesApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    *out = DevicesApiSnapshot{};
    out->revision = state.revision;
    out->join_window_open = runtime_snapshot.join_window_open;
    out->join_window_seconds_left = runtime_snapshot.join_window_seconds_left;

    for (std::size_t i = 0; i < state.devices.size() && out->device_count < core::kMaxDevices; ++i) {
        const core::CoreDeviceRecord& device = state.devices[i];
        if (device.short_addr == core::kUnknownDeviceShortAddr) {
            continue;
        }

        DevicesApiDeviceSnapshot& api_device = out->devices[out->device_count++];
        api_device.short_addr = device.short_addr;
        api_device.online = device.online;
        api_device.power_on = device.power_on;
        api_device.reporting_state = to_device_reporting_state(runtime_snapshot.reporting_state[i]);
        api_device.last_report_at_ms = runtime_snapshot.last_report_at_ms[i];
        api_device.stale = runtime_snapshot.stale[i];
        api_device.has_temperature = device.has_temperature;
        api_device.temperature_centi_c = device.temperature_centi_c;
        api_device.occupancy_state = to_device_occupancy_state(device.occupancy_state);
        api_device.contact_state = to_device_contact_state(device.contact_state);
        api_device.contact_tamper = device.contact_tamper;
        api_device.contact_battery_low = device.contact_battery_low;
        api_device.has_battery = runtime_snapshot.has_battery[i];
        api_device.battery_percent = runtime_snapshot.battery_percent[i];
        api_device.has_battery_voltage = runtime_snapshot.has_battery_voltage[i];
        api_device.battery_voltage_mv = runtime_snapshot.battery_voltage_mv[i];
        api_device.has_lqi = runtime_snapshot.has_lqi[i];
        api_device.lqi = runtime_snapshot.lqi[i];
        api_device.has_rssi = runtime_snapshot.has_rssi[i];
        api_device.rssi_dbm = runtime_snapshot.rssi_dbm[i];
        api_device.force_remove_ms_left = runtime_snapshot.force_remove_ms_left[i];
        api_device.force_remove_armed = api_device.force_remove_ms_left > 0U;
    }

    return true;
}

}  // namespace service
