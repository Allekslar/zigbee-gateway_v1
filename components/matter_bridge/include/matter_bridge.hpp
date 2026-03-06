/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "core_state.hpp"
#include "matter_endpoint_map.hpp"

namespace matter_bridge {

enum class MatterAttributeType : uint8_t {
    kAvailabilityOnline = 0,
    kTemperatureCentiC,
    kOccupancy,
    kContactOpen,
    kStale,
};

struct MatterAttributeUpdate {
    uint16_t short_addr{0};
    uint16_t endpoint{0};
    MatterAttributeType type{MatterAttributeType::kAvailabilityOnline};
    bool bool_value{false};
    int32_t int_value{0};
};

constexpr std::size_t kMatterMaxEndpointMapEntries = core::kMaxDevices;
constexpr std::size_t kMatterMaxUpdatesPerSync = core::kMaxDevices * 5U;

class MatterBridge {
public:
    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;
    bool set_endpoint_map(const MatterEndpointMapEntry* map, std::size_t size) noexcept;
    std::size_t sync_snapshot(const core::CoreState& state) noexcept;
    std::size_t drain_attribute_updates(MatterAttributeUpdate* out, std::size_t capacity) noexcept;

private:
    struct DeviceShadow {
        bool in_use{false};
        uint16_t short_addr{0};
        uint16_t status_endpoint{0};
        bool online{false};
        bool stale{false};
        bool has_temperature{false};
        int16_t temperature_centi_c{0};
        core::CoreOccupancyState occupancy{core::CoreOccupancyState::kUnknown};
        core::CoreContactState contact{core::CoreContactState::kUnknown};
    };

    void reset_sync_state() noexcept;

    bool started_{false};
    MatterEndpointMapEntry endpoint_map_[kMatterMaxEndpointMapEntries]{};
    std::size_t endpoint_map_size_{0};
    DeviceShadow cached_devices_[core::kMaxDevices]{};
    std::size_t pending_update_count_{0};
    MatterAttributeUpdate pending_updates_[kMatterMaxUpdatesPerSync]{};
};

}  // namespace matter_bridge
