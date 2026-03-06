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

bool map_default_endpoint_for_class(MatterDeviceClass device_class, uint16_t* endpoint_out) noexcept {
    if (endpoint_out == nullptr) {
        return false;
    }

    switch (device_class) {
        case MatterDeviceClass::kTemperature:
            *endpoint_out = kMatterEndpointTemperature;
            return true;
        case MatterDeviceClass::kOccupancy:
            *endpoint_out = kMatterEndpointOccupancy;
            return true;
        case MatterDeviceClass::kContact:
            *endpoint_out = kMatterEndpointContact;
            return true;
        case MatterDeviceClass::kUnknown:
        default:
            return false;
    }
}

bool map_resolve_endpoint(const MatterEndpointMapEntry* map,
                          std::size_t size,
                          uint16_t zigbee_short_addr,
                          MatterDeviceClass device_class,
                          uint16_t* endpoint_out) noexcept {
    if (endpoint_out == nullptr) {
        return false;
    }

    // Explicit per-device mapping has priority over class default mapping.
    if (map_find_endpoint(map, size, zigbee_short_addr, endpoint_out)) {
        return true;
    }

    return map_default_endpoint_for_class(device_class, endpoint_out);
}

}  // namespace matter_bridge
