/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "matter_endpoint_map.hpp"

namespace matter_bridge {

bool map_find_endpoint(const MatterEndpointMapEntry* map,
                       std::size_t size,
                       uint16_t zigbee_short_addr,
                       uint16_t* endpoint_out) noexcept {
    if (map == nullptr || endpoint_out == nullptr) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (map[i].zigbee_short_addr == zigbee_short_addr) {
            *endpoint_out = map[i].matter_endpoint;
            return true;
        }
    }

    return false;
}

}  // namespace matter_bridge
