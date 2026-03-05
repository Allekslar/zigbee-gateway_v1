/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core_effects.hpp"
#include "core_events.hpp"

namespace core {

inline constexpr std::size_t kMaxDevices = 64;
inline constexpr std::size_t kMaxEffectsPerReduce = 4;

enum class CoreReportingState : uint8_t {
    kUnknown = 0,
    kInterviewCompleted,
    kBindingReady,
    kReportingConfigured,
    kReportingActive,
    kStale,
};

enum class CoreOccupancyState : uint8_t {
    kUnknown = 0,
    kNotOccupied,
    kOccupied,
};

enum class CoreContactState : uint8_t {
    kUnknown = 0,
    kClosed,
    kOpen,
};

struct CoreDeviceRecord {
    uint16_t short_addr{kUnknownDeviceShortAddr};
    bool online{false};
    bool power_on{false};
    CoreReportingState reporting_state{CoreReportingState::kUnknown};
    uint32_t last_report_at_ms{0};
    bool stale{false};
    int16_t temperature_centi_c{0};
    bool has_temperature{false};
    CoreOccupancyState occupancy_state{CoreOccupancyState::kUnknown};
    CoreContactState contact_state{CoreContactState::kUnknown};
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
};

struct CoreState {
    uint32_t revision{0};
    uint16_t device_count{0};
    bool network_connected{false};
    uint8_t last_command_status{0};
    std::array<CoreDeviceRecord, kMaxDevices> devices{};
};

struct CoreEffectList {
    std::array<CoreEffect, kMaxEffectsPerReduce> items{};
    uint8_t count{0};

    bool push(const CoreEffect& effect) noexcept {
        if (static_cast<std::size_t>(count) >= items.size()) {
            return false;
        }

        items[count++] = effect;
        return true;
    }
};

struct CoreReduceResult {
    CoreState next{};
    CoreEffectList effects{};
};

CoreReduceResult core_reduce(const CoreState& prev, const CoreEvent& event) noexcept;

}  // namespace core
