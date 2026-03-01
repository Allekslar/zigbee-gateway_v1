/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace matter_bridge {

struct MatterEndpointMapEntry {
    uint16_t zigbee_short_addr{0};
    uint16_t matter_endpoint{0};
};

bool map_find_endpoint(const MatterEndpointMapEntry* map,
                       std::size_t size,
                       uint16_t zigbee_short_addr,
                       uint16_t* endpoint_out) noexcept;

}  // namespace matter_bridge
