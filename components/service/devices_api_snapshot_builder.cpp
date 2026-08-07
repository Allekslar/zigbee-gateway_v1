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
    const DeviceDescriptorStore& identity_store,
    const DeviceLocatorRegistry& locator_registry,
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
        // FD-01: a device belongs in this API surface once it has a durable
        // identity, regardless of whether it currently has a locator (a
        // persisted device that has not yet rejoined after reboot still
        // has a device_id, but has_locator below will correctly read
        // false for it -- see the DeviceLocatorRegistry lookup).
        if (!device.device_id.valid()) {
            continue;
        }

        DevicesApiDeviceSnapshot& api_device = out->devices[out->device_count++];
        if (!device.device_id.format(api_device.device_id_hex.data(), api_device.device_id_hex.size())) {
            api_device.device_id_hex[0] = '\0';
        }

        DeviceLocatorEntry locator_entry{};
        if (locator_registry.find_by_device_id(device.device_id, &locator_entry) &&
            locator_entry.status == DeviceLocatorStatus::kOnline) {
            api_device.has_locator = true;
            api_device.short_addr = locator_entry.short_addr;
            api_device.locator_revision = locator_entry.mapping_revision;
        } else {
            api_device.has_locator = false;
            api_device.short_addr = core::kUnknownDeviceShortAddr;
            api_device.locator_revision = 0U;
        }

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

        const DeviceDescriptorEntry* identity = identity_store.find(device.short_addr);
        if (identity != nullptr) {
            api_device.identity_status = identity->status;
            api_device.manufacturer = identity->manufacturer;
            api_device.model = identity->model;
        }
    }

    return true;
}

}  // namespace service
