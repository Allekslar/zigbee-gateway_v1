/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "core_events.hpp"
#include "core_state.hpp"

namespace mqtt_bridge {

constexpr std::size_t kMqttPayloadMaxLen = 384U;

struct MqttMessage {
    const char* topic{nullptr};
    char payload[kMqttPayloadMaxLen]{};
};

MqttMessage serialize_event(const core::CoreEvent& event) noexcept;

struct MqttSensorSnapshot {
    bool has_temperature{false};
    int16_t temperature_centi_c{0};

    core::CoreOccupancyState occupancy{core::CoreOccupancyState::kUnknown};
    core::CoreContactState contact_state{core::CoreContactState::kUnknown};
    bool contact_tamper{false};
    bool contact_battery_low{false};

    bool has_battery_percent{false};
    uint8_t battery_percent{0};
    bool has_battery_voltage{false};
    uint16_t battery_voltage_mv{0};

    bool has_lqi{false};
    uint8_t lqi{0};
    bool has_rssi{false};
    int8_t rssi_dbm{0};

    bool stale{false};
    uint32_t timestamp_ms{0};
};

bool serialize_sensor_payload(
    const MqttSensorSnapshot& snapshot,
    char* out,
    std::size_t out_size,
    std::size_t* out_len) noexcept;

}  // namespace mqtt_bridge
