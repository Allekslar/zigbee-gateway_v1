/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "matter_endpoint_map.hpp"

int main() {
    using namespace matter_bridge;

    MatterEndpointMapEntry map[] = {
        {0x2201U, 50U},
        {0x2202U, 51U},
    };

    uint16_t endpoint = 0;
    assert(map_find_endpoint(map, 2U, 0x2201U, &endpoint));
    assert(endpoint == 50U);
    assert(map_find_endpoint(map, 2U, 0x2202U, &endpoint));
    assert(endpoint == 51U);

    assert(!map_find_endpoint(map, 2U, 0x3333U, &endpoint));
    assert(!map_find_endpoint(nullptr, 2U, 0x2201U, &endpoint));
    assert(!map_find_endpoint(map, 2U, 0x2201U, nullptr));

    assert(map_default_endpoint_for_class(MatterDeviceClass::kTemperature, &endpoint));
    assert(endpoint == kMatterEndpointTemperature);
    assert(map_default_endpoint_for_class(MatterDeviceClass::kOccupancy, &endpoint));
    assert(endpoint == kMatterEndpointOccupancy);
    assert(map_default_endpoint_for_class(MatterDeviceClass::kContact, &endpoint));
    assert(endpoint == kMatterEndpointContact);
    assert(!map_default_endpoint_for_class(MatterDeviceClass::kUnknown, &endpoint));

    // Explicit per-device map must override class default.
    endpoint = 0;
    assert(map_resolve_endpoint(map, 2U, 0x2201U, MatterDeviceClass::kTemperature, &endpoint));
    assert(endpoint == 50U);

    // Fallback to class default if explicit entry is absent.
    endpoint = 0;
    assert(map_resolve_endpoint(map, 2U, 0x9999U, MatterDeviceClass::kOccupancy, &endpoint));
    assert(endpoint == kMatterEndpointOccupancy);

    endpoint = 0;
    assert(map_resolve_endpoint(nullptr, 0U, 0x9999U, MatterDeviceClass::kContact, &endpoint));
    assert(endpoint == kMatterEndpointContact);

    assert(!map_resolve_endpoint(nullptr, 0U, 0x9999U, MatterDeviceClass::kUnknown, &endpoint));
    assert(!map_resolve_endpoint(map, 2U, 0x2201U, MatterDeviceClass::kTemperature, nullptr));

    return 0;
}
