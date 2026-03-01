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

struct CoreDeviceRecord {
    uint16_t short_addr{kUnknownDeviceShortAddr};
    bool online{false};
    bool power_on{false};
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
