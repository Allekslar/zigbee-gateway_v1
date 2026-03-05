/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "core_state.hpp"

namespace web_ui {

struct DeviceDto {
    uint16_t short_addr{0};
    bool online{false};
    bool power_on{false};
    core::CoreReportingState reporting_state{core::CoreReportingState::kUnknown};
    uint32_t last_report_at_ms{0};
    bool stale{false};
    int16_t temperature_centi_c{0};
    bool has_temperature{false};
    core::CoreOccupancyState occupancy_state{core::CoreOccupancyState::kUnknown};
    core::CoreContactState contact_state{core::CoreContactState::kUnknown};
    bool contact_tamper{false};
    bool contact_battery_low{false};
    uint8_t battery_percent{0};
    bool has_battery{false};
    uint16_t battery_voltage_mv{0};
    bool has_battery_voltage{false};
    uint8_t lqi{0};
    bool has_lqi{false};
    int8_t rssi_dbm{0};
    bool has_rssi{false};
    bool force_remove_armed{false};
    uint32_t force_remove_ms_left{0};
};

struct NetworkDto {
    bool connected{false};
    uint8_t channel{0};
};

}  // namespace web_ui
