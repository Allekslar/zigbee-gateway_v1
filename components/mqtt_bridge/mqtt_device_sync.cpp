/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_bridge.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mqtt_bridge {
namespace {

bool is_active_device(const service::MqttBridgeDeviceSnapshot& device) noexcept {
    return device.short_addr != core::kUnknownDeviceShortAddr && device.online;
}

const service::MqttBridgeDeviceSnapshot* find_device_by_short(
    const service::MqttBridgeDeviceSnapshot* devices,
    const uint16_t count,
    const uint16_t short_addr) noexcept {
    for (uint16_t i = 0; i < count; ++i) {
        if (devices[i].short_addr == short_addr) {
            return &devices[i];
        }
    }
    return nullptr;
}

bool telemetry_fields_equal(
    const service::MqttBridgeDeviceSnapshot& a,
    const service::MqttBridgeDeviceSnapshot& b) noexcept {
    return a.has_temperature == b.has_temperature &&
           a.temperature_centi_c == b.temperature_centi_c &&
           a.occupancy_state == b.occupancy_state &&
           a.contact_state == b.contact_state &&
           a.contact_tamper == b.contact_tamper &&
           a.contact_battery_low == b.contact_battery_low &&
           a.has_battery == b.has_battery &&
           a.battery_percent == b.battery_percent &&
           a.has_battery_voltage == b.has_battery_voltage &&
           a.battery_voltage_mv == b.battery_voltage_mv &&
           a.has_lqi == b.has_lqi &&
           a.lqi == b.lqi &&
           a.has_rssi == b.has_rssi &&
           a.rssi_dbm == b.rssi_dbm &&
           a.stale == b.stale &&
           a.last_report_at_ms == b.last_report_at_ms;
}

bool build_availability_publication(
    const uint16_t short_addr,
    const bool online,
    MqttPublishedMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    if (!topic_device_availability(short_addr, out->topic, sizeof(out->topic))) {
        return false;
    }

    const int written = std::snprintf(out->payload, sizeof(out->payload), "%s", online ? "online" : "offline");
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(out->payload)) {
        return false;
    }

    out->retain = true;
    return true;
}

bool build_state_publication(const service::MqttBridgeDeviceSnapshot& device, MqttPublishedMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    if (!topic_device_state(device.short_addr, out->topic, sizeof(out->topic))) {
        return false;
    }

    const int written = std::snprintf(
        out->payload,
        sizeof(out->payload),
        "{\"power_on\":%s}",
        device.power_on ? "true" : "false");
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(out->payload)) {
        return false;
    }

    out->retain = true;
    return true;
}

bool build_telemetry_publication(
    const service::MqttBridgeDeviceSnapshot& device,
    MqttPublishedMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    if (!topic_device_telemetry(device.short_addr, out->topic, sizeof(out->topic))) {
        return false;
    }

    MqttSensorSnapshot snapshot{};
    snapshot.has_temperature = device.has_temperature;
    snapshot.temperature_centi_c = device.temperature_centi_c;
    snapshot.occupancy = device.occupancy_state;
    snapshot.contact_state = device.contact_state;
    snapshot.contact_tamper = device.contact_tamper;
    snapshot.contact_battery_low = device.contact_battery_low;
    snapshot.has_battery_percent = device.has_battery;
    snapshot.battery_percent = device.battery_percent;
    snapshot.has_battery_voltage = device.has_battery_voltage;
    snapshot.battery_voltage_mv = device.battery_voltage_mv;
    snapshot.has_lqi = device.has_lqi;
    snapshot.lqi = device.lqi;
    snapshot.has_rssi = device.has_rssi;
    snapshot.rssi_dbm = device.rssi_dbm;
    snapshot.stale = device.stale;
    snapshot.timestamp_ms = device.last_report_at_ms;

    std::size_t payload_len = 0;
    if (!serialize_sensor_payload(snapshot, out->payload, sizeof(out->payload), &payload_len) || payload_len == 0U) {
        return false;
    }

    out->retain = true;
    return true;
}

}  // namespace

std::size_t MqttBridge::sync_snapshot(const service::MqttBridgeSnapshot& snapshot) noexcept {
    if (!started_) {
        return 0U;
    }

    pending_publication_count_ = 0;
    auto enqueue_publication = [&](const MqttPublishedMessage& publication) noexcept -> bool {
        if (pending_publication_count_ >= kMaxMqttPublicationsPerSync) {
            return false;
        }
        pending_publications_[pending_publication_count_++] = publication;
        return true;
    };

    uint16_t next_count = 0;

    for (std::size_t i = 0; i < snapshot.device_count && next_count < snapshot.devices.size(); ++i) {
        const service::MqttBridgeDeviceSnapshot& current = snapshot.devices[i];
        if (!is_active_device(current)) {
            continue;
        }

        sync_devices_scratch_[next_count++] = current;

        const service::MqttBridgeDeviceSnapshot* previous = nullptr;
        if (cache_initialized_) {
            previous = find_device_by_short(cached_devices_, cached_device_count_, current.short_addr);
        }

        bool publish_availability = false;
        bool publish_state = false;
        bool publish_telemetry = false;

        if (previous == nullptr) {
            publish_availability = true;
            publish_state = true;
            publish_telemetry = true;
        } else {
            publish_state = previous->power_on != current.power_on;
            publish_telemetry = !telemetry_fields_equal(*previous, current);
        }

        MqttPublishedMessage publication{};
        if (publish_availability && build_availability_publication(current.short_addr, true, &publication)) {
            (void)enqueue_publication(publication);
        }
        if (publish_state && build_state_publication(current, &publication)) {
            (void)enqueue_publication(publication);
        }
        if (publish_telemetry && build_telemetry_publication(current, &publication)) {
            (void)enqueue_publication(publication);
        }
    }

    if (cache_initialized_) {
        for (uint16_t i = 0; i < cached_device_count_; ++i) {
            const uint16_t short_addr = cached_devices_[i].short_addr;
            if (find_device_by_short(sync_devices_scratch_, next_count, short_addr) != nullptr) {
                continue;
            }

            MqttPublishedMessage availability{};
            if (build_availability_publication(short_addr, false, &availability)) {
                (void)enqueue_publication(availability);
            }
        }
    }

    std::memcpy(cached_devices_, sync_devices_scratch_, sizeof(sync_devices_scratch_));
    cached_device_count_ = next_count;
    cache_initialized_ = true;

    return pending_publication_count_;
}

std::size_t MqttBridge::drain_publications(MqttPublishedMessage* out, const std::size_t capacity) noexcept {
    if (out == nullptr || capacity == 0U || pending_publication_count_ == 0U) {
        return 0U;
    }

    const std::size_t to_copy = std::min(capacity, pending_publication_count_);
    for (std::size_t i = 0; i < to_copy; ++i) {
        out[i] = pending_publications_[i];
    }

    const std::size_t remaining = pending_publication_count_ - to_copy;
    for (std::size_t i = 0; i < remaining; ++i) {
        pending_publications_[i] = pending_publications_[i + to_copy];
    }
    pending_publication_count_ = remaining;

    return to_copy;
}

void sync_device_state(uint16_t short_addr, bool on) noexcept {
    (void)short_addr;
    (void)on;
}

}  // namespace mqtt_bridge
