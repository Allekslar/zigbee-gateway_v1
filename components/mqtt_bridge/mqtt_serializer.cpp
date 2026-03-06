/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_serializer.hpp"

#include <cstdio>

namespace mqtt_bridge {
namespace {

const char* occupancy_to_string(const core::CoreOccupancyState state) noexcept {
    switch (state) {
        case core::CoreOccupancyState::kNotOccupied:
            return "not_occupied";
        case core::CoreOccupancyState::kOccupied:
            return "occupied";
        case core::CoreOccupancyState::kUnknown:
        default:
            return "unknown";
    }
}

const char* contact_to_string(const core::CoreContactState state) noexcept {
    switch (state) {
        case core::CoreContactState::kClosed:
            return "closed";
        case core::CoreContactState::kOpen:
            return "open";
        case core::CoreContactState::kUnknown:
        default:
            return "unknown";
    }
}

}  // namespace

MqttMessage serialize_event(const core::CoreEvent& event) noexcept {
    MqttMessage message{};
    message.topic = "zigbee-gateway/state";

    const int written = std::snprintf(
        message.payload,
        sizeof(message.payload),
        "{\"event\":%u,\"short_addr\":%u,\"correlation_id\":%lu}",
        static_cast<unsigned>(event.type),
        static_cast<unsigned>(event.device_short_addr),
        static_cast<unsigned long>(event.correlation_id));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(message.payload)) {
        message.payload[0] = '\0';
    }

    return message;
}

bool serialize_sensor_payload(
    const MqttSensorSnapshot& snapshot,
    char* out,
    const std::size_t out_size,
    std::size_t* out_len) noexcept {
    if (out == nullptr || out_size == 0U) {
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    char temperature_buf[16] = "null";
    if (snapshot.has_temperature) {
        const int whole = snapshot.temperature_centi_c / 100;
        const int frac = snapshot.temperature_centi_c >= 0
                             ? (snapshot.temperature_centi_c % 100)
                             : -(snapshot.temperature_centi_c % 100);
        const int written = std::snprintf(temperature_buf, sizeof(temperature_buf), "%d.%02d", whole, frac);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(temperature_buf)) {
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            out[0] = '\0';
            return false;
        }
    }

    char battery_percent_buf[8] = "null";
    if (snapshot.has_battery_percent) {
        const int written = std::snprintf(
            battery_percent_buf,
            sizeof(battery_percent_buf),
            "%u",
            static_cast<unsigned>(snapshot.battery_percent));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(battery_percent_buf)) {
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            out[0] = '\0';
            return false;
        }
    }

    char battery_voltage_buf[16] = "null";
    if (snapshot.has_battery_voltage) {
        const int written = std::snprintf(
            battery_voltage_buf,
            sizeof(battery_voltage_buf),
            "%u",
            static_cast<unsigned>(snapshot.battery_voltage_mv));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(battery_voltage_buf)) {
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            out[0] = '\0';
            return false;
        }
    }

    char lqi_buf[8] = "null";
    if (snapshot.has_lqi) {
        const int written = std::snprintf(lqi_buf, sizeof(lqi_buf), "%u", static_cast<unsigned>(snapshot.lqi));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(lqi_buf)) {
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            out[0] = '\0';
            return false;
        }
    }

    char rssi_buf[8] = "null";
    if (snapshot.has_rssi) {
        const int written = std::snprintf(rssi_buf, sizeof(rssi_buf), "%d", static_cast<int>(snapshot.rssi_dbm));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(rssi_buf)) {
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            out[0] = '\0';
            return false;
        }
    }

    const int written = std::snprintf(
        out,
        out_size,
        "{\"temperature_c\":%s,\"occupancy\":\"%s\",\"contact\":{\"state\":\"%s\",\"tamper\":%s,\"battery_low\":%s},"
        "\"battery\":{\"percent\":%s,\"voltage_mv\":%s},\"lqi\":%s,\"rssi\":%s,\"stale\":%s,\"timestamp_ms\":%lu}",
        temperature_buf,
        occupancy_to_string(snapshot.occupancy),
        contact_to_string(snapshot.contact_state),
        snapshot.contact_tamper ? "true" : "false",
        snapshot.contact_battery_low ? "true" : "false",
        battery_percent_buf,
        battery_voltage_buf,
        lqi_buf,
        rssi_buf,
        snapshot.stale ? "true" : "false",
        static_cast<unsigned long>(snapshot.timestamp_ms));

    if (written <= 0 || static_cast<std::size_t>(written) >= out_size) {
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        out[0] = '\0';
        return false;
    }

    if (out_len != nullptr) {
        *out_len = static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace mqtt_bridge
