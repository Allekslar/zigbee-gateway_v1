/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "service_public_types.hpp"

namespace service {

struct DevicePowerCommandRequest {
    uint32_t correlation_id{0};
    uint16_t short_addr{kUnknownShortAddr};
    bool desired_power_on{false};
    uint32_t issued_at_ms{0};
};

}  // namespace service
