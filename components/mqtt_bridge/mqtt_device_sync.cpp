/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace mqtt_bridge {
namespace {

uint32_t monotonic_now_ms() noexcept {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool is_active_device(const service::MqttBridgeDeviceSnapshot& device) noexcept {
    return device.short_addr != service::kUnknownShortAddr && device.online;
}

// Identity-keyed lookup (plan S4 MQTT #15): a device is "the same device"
// across syncs iff its DeviceId matches, regardless of short_addr, so a
// short_addr remap is a locator update, not a disappearance+recreation --
// this is what keeps a remap from ever touching a legacy topic again
// after cutover (only the one-time first-sync tombstone sweep below ever
// writes to a legacy topic, and it fires at most once per boot).
const service::MqttBridgeDeviceSnapshot* find_device_by_device_id_hex(
    const service::MqttBridgeDeviceSnapshot* devices,
    const uint16_t count,
    const char* device_id_hex) noexcept {
    if (devices == nullptr || device_id_hex == nullptr || device_id_hex[0] == '\0') {
        return nullptr;
    }
    for (uint16_t i = 0; i < count; ++i) {
        if (devices[i].device_id_hex[0] != '\0' && std::strcmp(devices[i].device_id_hex.data(), device_id_hex) == 0) {
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

bool build_v1_availability_publication(
    const char* gateway_id_hex,
    const char* device_id_hex,
    const uint32_t revision,
    const bool online,
    MqttPublishedMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    if (!topic_v1_device_availability(device_id_hex, out->topic, sizeof(out->topic))) {
        return false;
    }

    std::size_t payload_len = 0U;
    if (!serialize_v1_availability_payload(
            gateway_id_hex, device_id_hex, revision, online, monotonic_now_ms(), out->payload, sizeof(out->payload),
            &payload_len) ||
        payload_len == 0U) {
        return false;
    }

    out->retain = true;
    return true;
}

bool build_v1_state_publication(
    const char* gateway_id_hex,
    const uint32_t revision,
    const service::MqttBridgeDeviceSnapshot& device,
    MqttPublishedMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    if (!topic_v1_device_state(device.device_id_hex.data(), out->topic, sizeof(out->topic))) {
        return false;
    }

    std::size_t payload_len = 0U;
    if (!serialize_v1_state_payload(
            gateway_id_hex,
            device.device_id_hex.data(),
            revision,
            device.power_on,
            monotonic_now_ms(),
            out->payload,
            sizeof(out->payload),
            &payload_len) ||
        payload_len == 0U) {
        return false;
    }

    out->retain = true;
    return true;
}

bool build_v1_telemetry_publication(
    const char* gateway_id_hex,
    const uint32_t revision,
    const service::MqttBridgeDeviceSnapshot& device,
    MqttPublishedMessage* out) noexcept {
    if (out == nullptr) {
        return false;
    }

    if (!topic_v1_device_telemetry(device.device_id_hex.data(), out->topic, sizeof(out->topic))) {
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
    if (!serialize_v1_sensor_payload(
            gateway_id_hex, device.device_id_hex.data(), revision, snapshot, out->payload, sizeof(out->payload),
            &payload_len) ||
        payload_len == 0U) {
        return false;
    }

    out->retain = true;
    return true;
}

// One-time legacy retained-topic cleanup (plan S4 MQTT #14): an empty
// retained payload clears whatever real content the legacy topic tree
// carried before this firmware's v1 cutover. Takes a legacy topic builder
// by function pointer so the three legacy topics (state/telemetry/
// availability) share one implementation instead of three near-identical
// copies.
bool build_legacy_tombstone_publication(
    bool (*legacy_topic_builder)(uint16_t, char*, std::size_t),
    const uint16_t short_addr,
    MqttPublishedMessage* out) noexcept {
    if (out == nullptr || legacy_topic_builder == nullptr) {
        return false;
    }

    if (!legacy_topic_builder(short_addr, out->topic, sizeof(out->topic))) {
        return false;
    }

    out->payload[0] = '\0';
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
        if (!ensure_publication_queue() || pending_publication_count_ >= pending_publication_capacity_) {
            return false;
        }
        pending_publications_[pending_publication_count_++] = publication;
        return true;
    };

    // Captured before cache_initialized_ is set true at the end of this
    // call: true only for the very first sync after start()/
    // reset_sync_cache(), which is exactly the "during upgrade" moment
    // plan #14 describes. Every later sync (including one triggered by a
    // remap, which looks identical to a fresh device under a short_addr-
    // only view) leaves legacy topics untouched, satisfying #15.
    const bool first_sync = !cache_initialized_;
    ensure_gateway_id_hex();

    uint16_t next_count = 0;

    for (std::size_t i = 0; i < snapshot.device_count && next_count < snapshot.devices.size(); ++i) {
        const service::MqttBridgeDeviceSnapshot& current = snapshot.devices[i];
        if (!is_active_device(current)) {
            continue;
        }

        sync_devices_scratch_[next_count++] = current;

        if (first_sync) {
            MqttPublishedMessage tombstone{};
            if (build_legacy_tombstone_publication(&topic_device_state, current.short_addr, &tombstone)) {
                (void)enqueue_publication(tombstone);
            }
            if (build_legacy_tombstone_publication(&topic_device_telemetry, current.short_addr, &tombstone)) {
                (void)enqueue_publication(tombstone);
            }
            if (build_legacy_tombstone_publication(&topic_device_availability, current.short_addr, &tombstone)) {
                (void)enqueue_publication(tombstone);
            }
        }

        if (current.device_id_hex[0] == '\0' || !gateway_id_hex_ready_) {
            // No resolved identity (or no resolved gateway_id) yet: this
            // device cannot be published under any v1 topic. It still
            // occupies its slot in the identity-keyed cache above (empty
            // device_id_hex), so it is correctly ignored, not mistaken for
            // "gone", once its identity does resolve on a later sync.
            continue;
        }

        const service::MqttBridgeDeviceSnapshot* previous = nullptr;
        if (cache_initialized_) {
            previous = find_device_by_device_id_hex(cached_devices_, cached_device_count_, current.device_id_hex.data());
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
        if (publish_availability &&
            build_v1_availability_publication(
                gateway_id_hex_, current.device_id_hex.data(), snapshot.revision, true, &publication)) {
            (void)enqueue_publication(publication);
        }
        if (publish_state && build_v1_state_publication(gateway_id_hex_, snapshot.revision, current, &publication)) {
            (void)enqueue_publication(publication);
        }
        if (publish_telemetry &&
            build_v1_telemetry_publication(gateway_id_hex_, snapshot.revision, current, &publication)) {
            (void)enqueue_publication(publication);
        }
    }

    if (cache_initialized_) {
        for (uint16_t i = 0; i < cached_device_count_; ++i) {
            const service::MqttBridgeDeviceSnapshot& cached = cached_devices_[i];
            if (cached.device_id_hex[0] == '\0') {
                continue;
            }
            if (find_device_by_device_id_hex(sync_devices_scratch_, next_count, cached.device_id_hex.data()) !=
                nullptr) {
                continue;
            }

            MqttPublishedMessage availability{};
            if (gateway_id_hex_ready_ &&
                build_v1_availability_publication(
                    gateway_id_hex_, cached.device_id_hex.data(), snapshot.revision, false, &availability)) {
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
    if (out == nullptr || capacity == 0U || pending_publications_ == nullptr || pending_publication_count_ == 0U) {
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

}  // namespace mqtt_bridge
