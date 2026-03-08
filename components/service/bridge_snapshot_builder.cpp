/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "bridge_snapshot_builder.hpp"

namespace service {

bool BridgeSnapshotBuilder::build_mqtt_snapshot(MqttBridgeSnapshot* out) const noexcept {
    if (out == nullptr || registry_ == nullptr) {
        return false;
    }

    core::CoreRegistry::SnapshotRef snapshot{};
    if (!registry_->pin_current(&snapshot) || !snapshot.valid()) {
        return false;
    }

    out->revision = snapshot.state->revision;
    out->device_count = 0U;

    for (std::size_t i = 0; i < snapshot.state->devices.size() && out->device_count < core::kMaxDevices; ++i) {
        const core::CoreDeviceRecord& device = snapshot.state->devices[i];
        if (device.short_addr == core::kUnknownDeviceShortAddr || !device.online) {
            continue;
        }

        MqttBridgeDeviceSnapshot& mqtt_device = out->devices[out->device_count++];
        mqtt_device.short_addr = device.short_addr;
        mqtt_device.online = device.online;
        mqtt_device.power_on = device.power_on;
        mqtt_device.has_temperature = device.has_temperature;
        mqtt_device.temperature_centi_c = device.temperature_centi_c;
        mqtt_device.occupancy_state = device.occupancy_state;
        mqtt_device.contact_state = device.contact_state;
        mqtt_device.contact_tamper = device.contact_tamper;
        mqtt_device.contact_battery_low = device.contact_battery_low;
        mqtt_device.has_battery = device.has_battery;
        mqtt_device.battery_percent = device.battery_percent;
        mqtt_device.has_battery_voltage = device.has_battery_voltage;
        mqtt_device.battery_voltage_mv = device.battery_voltage_mv;
        mqtt_device.has_lqi = device.has_lqi;
        mqtt_device.lqi = device.lqi;
        mqtt_device.has_rssi = device.has_rssi;
        mqtt_device.rssi_dbm = device.rssi_dbm;
        mqtt_device.stale = device.stale;
        mqtt_device.last_report_at_ms = device.last_report_at_ms;
    }

    registry_->release_snapshot(&snapshot);
    return true;
}

bool BridgeSnapshotBuilder::build_matter_snapshot(MatterBridgeSnapshot* out) const noexcept {
    if (out == nullptr || registry_ == nullptr) {
        return false;
    }

    core::CoreRegistry::SnapshotRef snapshot{};
    if (!registry_->pin_current(&snapshot) || !snapshot.valid()) {
        return false;
    }

    out->revision = snapshot.state->revision;
    out->device_count = 0U;

    for (std::size_t i = 0; i < snapshot.state->devices.size() && out->device_count < core::kMaxDevices; ++i) {
        const core::CoreDeviceRecord& device = snapshot.state->devices[i];
        if (device.short_addr == core::kUnknownDeviceShortAddr || !device.online) {
            continue;
        }

        MatterBridgeDeviceSnapshot& matter_device = out->devices[out->device_count++];
        matter_device.short_addr = device.short_addr;
        matter_device.online = device.online;
        matter_device.stale = device.stale;
        matter_device.has_temperature = device.has_temperature;
        matter_device.temperature_centi_c = device.temperature_centi_c;
        matter_device.has_occupancy = device.occupancy_state != core::CoreOccupancyState::kUnknown;
        matter_device.occupied = device.occupancy_state == core::CoreOccupancyState::kOccupied;
        matter_device.has_contact = device.contact_state != core::CoreContactState::kUnknown;
        matter_device.contact_open = device.contact_state == core::CoreContactState::kOpen;

        if (matter_device.has_temperature) {
            matter_device.primary_class = MatterBridgeDeviceClass::kTemperature;
        } else if (matter_device.has_occupancy) {
            matter_device.primary_class = MatterBridgeDeviceClass::kOccupancy;
        } else if (matter_device.has_contact) {
            matter_device.primary_class = MatterBridgeDeviceClass::kContact;
        }
    }

    registry_->release_snapshot(&snapshot);
    return true;
}

}  // namespace service
