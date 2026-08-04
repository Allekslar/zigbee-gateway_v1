/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "persisted_device_state.hpp"

namespace service {

PersistedStatePayload to_payload(const core::CoreState& state) noexcept {
    PersistedStatePayload payload{};

    uint32_t out_index = 0U;
    for (const core::CoreDeviceRecord& device : state.devices) {
        if (!device.device_id.valid()) {
            // FD-01: an identity-less record is never carried into the
            // explicit persisted schema. This is the "quarantine by
            // omission" path documented in docs/architecture/DEVICE_IDENTITY.md
            // for records that predate S2's DeviceId introduction.
            continue;
        }
        if (out_index >= payload.devices.size()) {
            break;  // core::kMaxDevices already bounds this; defensive only.
        }

        PersistedDeviceRecord& out = payload.devices[out_index];
        out.valid = true;
        out.device_id_bytes = device.device_id.bytes();
        out.last_short_addr = device.short_addr;
        out.power_on = device.power_on;
        out.reporting_state = static_cast<uint8_t>(device.reporting_state);
        out.has_temperature = device.has_temperature;
        out.temperature_centi_c = device.temperature_centi_c;
        out.occupancy_state = static_cast<uint8_t>(device.occupancy_state);
        out.contact_state = static_cast<uint8_t>(device.contact_state);
        out.contact_tamper = device.contact_tamper;
        out.contact_battery_low = device.contact_battery_low;
        out.has_battery = device.has_battery;
        out.battery_percent = device.battery_percent;
        out.has_battery_voltage = device.has_battery_voltage;
        out.battery_voltage_mv = device.battery_voltage_mv;
        out.has_lqi = device.has_lqi;
        out.lqi = device.lqi;
        out.has_rssi = device.has_rssi;
        out.rssi_dbm = device.rssi_dbm;
        // Matter endpoint assignment is not yet produced anywhere (S4 is the
        // sole allocator); a freshly-built payload always carries kUnassigned.
        ++out_index;
    }
    payload.device_count = out_index;

    return payload;
}

core::CoreState apply_payload(const PersistedStatePayload& payload) noexcept {
    core::CoreState state{};

    uint32_t restored_count = 0U;
    const std::size_t limit =
        payload.device_count < payload.devices.size() ? payload.device_count : payload.devices.size();
    for (std::size_t i = 0; i < limit && restored_count < state.devices.size(); ++i) {
        const PersistedDeviceRecord& in = payload.devices[i];
        if (!in.valid) {
            continue;
        }
        const core::DeviceId device_id(in.device_id_bytes);
        if (!device_id.valid()) {
            continue;  // defensive: never restore a record with a corrupted/invalid identity.
        }

        core::CoreDeviceRecord& out = state.devices[restored_count];
        out.device_id = device_id;
        out.short_addr = in.last_short_addr;
        // Sanitize restored runtime fields (plan Section 9 S3): a device
        // restored from persisted state is never marked online -- it must
        // rejoin/report before Core considers it present again.
        out.online = false;
        out.power_on = in.power_on;
        out.reporting_state = static_cast<core::CoreReportingState>(in.reporting_state);
        out.last_report_at_ms = 0U;
        out.stale = false;
        out.has_temperature = in.has_temperature;
        out.temperature_centi_c = in.temperature_centi_c;
        out.occupancy_state = static_cast<core::CoreOccupancyState>(in.occupancy_state);
        out.contact_state = static_cast<core::CoreContactState>(in.contact_state);
        out.contact_tamper = in.contact_tamper;
        out.contact_battery_low = in.contact_battery_low;
        out.has_battery = in.has_battery;
        out.battery_percent = in.battery_percent;
        out.has_battery_voltage = in.has_battery_voltage;
        out.battery_voltage_mv = in.battery_voltage_mv;
        out.has_lqi = in.has_lqi;
        out.lqi = in.lqi;
        out.has_rssi = in.has_rssi;
        out.rssi_dbm = in.rssi_dbm;

        ++restored_count;
    }

    state.device_count = static_cast<uint16_t>(restored_count);
    state.network_connected = false;
    state.last_command_status = 0U;
    state.revision = 0U;
    return state;
}

}  // namespace service
