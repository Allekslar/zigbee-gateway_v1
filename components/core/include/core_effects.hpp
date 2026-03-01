/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "core_events.hpp"

namespace core {

enum class CoreEffectType : uint8_t {
    kNone = 0,
    kPersistState,
    kPublishTelemetry,
    kSetLed,
    kSendZigbeeOnOff,
    kRefreshNetwork,
    kEmitCommandResult,
};

struct CoreEffect {
    CoreEffectType type{CoreEffectType::kNone};
    uint32_t correlation_id{kNoCorrelationId};
    uint16_t device_short_addr{kUnknownDeviceShortAddr};
    uint32_t arg_u32{0};
    bool arg_bool{false};
};

}  // namespace core
