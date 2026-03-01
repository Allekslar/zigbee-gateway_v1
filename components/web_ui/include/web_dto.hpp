/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace web_ui {

struct DeviceDto {
    uint16_t short_addr{0};
    bool online{false};
    bool power_on{false};
};

struct NetworkDto {
    bool connected{false};
    uint8_t channel{0};
};

}  // namespace web_ui
