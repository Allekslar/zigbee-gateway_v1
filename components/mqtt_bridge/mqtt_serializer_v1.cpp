/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_serializer_v1.hpp"

#include <inttypes.h>
#include <cstdio>
#include <cstring>

#include "mqtt_topics_v1.hpp"

namespace mqtt_bridge {
namespace {

bool identity_valid(const char* gateway_id_hex, const char* device_id_hex) noexcept {
    if (gateway_id_hex == nullptr || std::strlen(gateway_id_hex) != kV1GatewayIdHexLength) {
        return false;
    }
    if (device_id_hex != nullptr && std::strlen(device_id_hex) != kV1DeviceIdHexLength) {
        return false;
    }
    return true;
}

// Mirrors mqtt_serializer.cpp's occupancy_to_string()/contact_to_string()
// exactly (kept as a separate anonymous-namespace copy here rather than
// shared, since those are file-local to the legacy serializer and this
// file is deliberately additive and does not modify it).
const char* v1_occupancy_to_string(const service::DeviceOccupancyState state) noexcept {
    switch (state) {
        case service::DeviceOccupancyState::kNotOccupied:
            return "not_occupied";
        case service::DeviceOccupancyState::kOccupied:
            return "occupied";
        case service::DeviceOccupancyState::kUnknown:
        default:
            return "unknown";
    }
}

const char* v1_contact_to_string(const service::DeviceContactState state) noexcept {
    switch (state) {
        case service::DeviceContactState::kClosed:
            return "closed";
        case service::DeviceContactState::kOpen:
            return "open";
        case service::DeviceContactState::kUnknown:
        default:
            return "unknown";
    }
}

}  // namespace

bool serialize_v1_state_payload(
    const char* gateway_id_hex,
    const char* device_id_hex,
    const uint32_t revision,
    const bool power_on,
    const uint32_t observed_at_ms,
    char* out,
    const std::size_t out_size,
    std::size_t* out_len) noexcept {
    if (out == nullptr || out_size == 0U) {
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }
    if (!identity_valid(gateway_id_hex, device_id_hex) || device_id_hex == nullptr) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    const int written = std::snprintf(
        out,
        out_size,
        "{\"schema_version\":1,\"gateway_id\":\"%s\",\"device_id\":\"%s\",\"revision\":%" PRIu32
        ",\"observed_at_ms\":%" PRIu32 ",\"power_on\":%s}",
        gateway_id_hex,
        device_id_hex,
        revision,
        observed_at_ms,
        power_on ? "true" : "false");
    if (written <= 0 || static_cast<std::size_t>(written) >= out_size) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    if (out_len != nullptr) {
        *out_len = static_cast<std::size_t>(written);
    }
    return true;
}

bool serialize_v1_availability_payload(
    const char* gateway_id_hex,
    const char* device_id_hex,
    const uint32_t revision,
    const bool online,
    const uint32_t observed_at_ms,
    char* out,
    const std::size_t out_size,
    std::size_t* out_len) noexcept {
    if (out == nullptr || out_size == 0U) {
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }
    if (!identity_valid(gateway_id_hex, device_id_hex) || device_id_hex == nullptr) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    const int written = std::snprintf(
        out,
        out_size,
        "{\"schema_version\":1,\"gateway_id\":\"%s\",\"device_id\":\"%s\",\"revision\":%" PRIu32
        ",\"observed_at_ms\":%" PRIu32 ",\"online\":%s}",
        gateway_id_hex,
        device_id_hex,
        revision,
        observed_at_ms,
        online ? "true" : "false");
    if (written <= 0 || static_cast<std::size_t>(written) >= out_size) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    if (out_len != nullptr) {
        *out_len = static_cast<std::size_t>(written);
    }
    return true;
}

bool serialize_v1_sensor_payload(
    const char* gateway_id_hex,
    const char* device_id_hex,
    const uint32_t revision,
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
    if (!identity_valid(gateway_id_hex, device_id_hex) || device_id_hex == nullptr) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    char temperature_buf[16] = "null";
    if (snapshot.has_temperature) {
        const int whole = snapshot.temperature_centi_c / 100;
        const int frac = snapshot.temperature_centi_c >= 0 ? (snapshot.temperature_centi_c % 100)
                                                             : -(snapshot.temperature_centi_c % 100);
        const int written = std::snprintf(temperature_buf, sizeof(temperature_buf), "%d.%02d", whole, frac);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(temperature_buf)) {
            out[0] = '\0';
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            return false;
        }
    }

    char battery_percent_buf[8] = "null";
    if (snapshot.has_battery_percent) {
        const int written = std::snprintf(
            battery_percent_buf, sizeof(battery_percent_buf), "%u", static_cast<unsigned>(snapshot.battery_percent));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(battery_percent_buf)) {
            out[0] = '\0';
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            return false;
        }
    }

    char battery_voltage_buf[16] = "null";
    if (snapshot.has_battery_voltage) {
        const int written = std::snprintf(
            battery_voltage_buf, sizeof(battery_voltage_buf), "%u", static_cast<unsigned>(snapshot.battery_voltage_mv));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(battery_voltage_buf)) {
            out[0] = '\0';
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            return false;
        }
    }

    char lqi_buf[8] = "null";
    if (snapshot.has_lqi) {
        const int written = std::snprintf(lqi_buf, sizeof(lqi_buf), "%u", static_cast<unsigned>(snapshot.lqi));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(lqi_buf)) {
            out[0] = '\0';
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            return false;
        }
    }

    char rssi_buf[8] = "null";
    if (snapshot.has_rssi) {
        const int written = std::snprintf(rssi_buf, sizeof(rssi_buf), "%d", static_cast<int>(snapshot.rssi_dbm));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(rssi_buf)) {
            out[0] = '\0';
            if (out_len != nullptr) {
                *out_len = 0U;
            }
            return false;
        }
    }

    const int written = std::snprintf(
        out,
        out_size,
        "{\"schema_version\":1,\"gateway_id\":\"%s\",\"device_id\":\"%s\",\"revision\":%" PRIu32
        ",\"temperature_c\":%s,\"occupancy\":\"%s\",\"contact\":{\"state\":\"%s\",\"tamper\":%s,\"battery_low\":%s},"
        "\"battery\":{\"percent\":%s,\"voltage_mv\":%s},\"lqi\":%s,\"rssi\":%s,\"stale\":%s,\"timestamp_ms\":%lu}",
        gateway_id_hex,
        device_id_hex,
        revision,
        temperature_buf,
        v1_occupancy_to_string(snapshot.occupancy),
        v1_contact_to_string(snapshot.contact_state),
        snapshot.contact_tamper ? "true" : "false",
        snapshot.contact_battery_low ? "true" : "false",
        battery_percent_buf,
        battery_voltage_buf,
        lqi_buf,
        rssi_buf,
        snapshot.stale ? "true" : "false",
        static_cast<unsigned long>(snapshot.timestamp_ms));
    if (written <= 0 || static_cast<std::size_t>(written) >= out_size) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    if (out_len != nullptr) {
        *out_len = static_cast<std::size_t>(written);
    }
    return true;
}

bool serialize_v1_gateway_state_payload(
    const char* gateway_id_hex, char* out, const std::size_t out_size, std::size_t* out_len) noexcept {
    if (out == nullptr || out_size == 0U) {
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }
    if (!identity_valid(gateway_id_hex, nullptr)) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    const int written =
        std::snprintf(out, out_size, "{\"schema_version\":1,\"gateway_id\":\"%s\"}", gateway_id_hex);
    if (written <= 0 || static_cast<std::size_t>(written) >= out_size) {
        out[0] = '\0';
        if (out_len != nullptr) {
            *out_len = 0U;
        }
        return false;
    }

    if (out_len != nullptr) {
        *out_len = static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace mqtt_bridge
