/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstdint>

#include "core_state.hpp"

namespace service {

enum class CommandSubmitStatus : uint8_t {
    kAccepted = 0,
    kInvalidArgument = 1,
    kBusy = 2,
    kNoCapacity = 3,
    kInternal = 4,
};

enum class MatterBridgeDeviceClass : uint8_t {
    kUnknown = 0,
    kTemperature = 1,
    kOccupancy = 2,
    kContact = 3,
};

struct MatterBridgeDeviceSnapshot {
    uint16_t short_addr{core::kUnknownDeviceShortAddr};
    bool online{false};
    bool stale{false};
    MatterBridgeDeviceClass primary_class{MatterBridgeDeviceClass::kUnknown};
    bool has_temperature{false};
    int16_t temperature_centi_c{0};
    bool has_occupancy{false};
    bool occupied{false};
    bool has_contact{false};
    bool contact_open{false};
};

struct MatterBridgeSnapshot {
    uint32_t revision{0};
    uint16_t device_count{0};
    std::array<MatterBridgeDeviceSnapshot, core::kMaxDevices> devices{};
};

// Narrow Matter-facing runtime seam: Matter bridge must depend on this contract
// instead of the full ServiceRuntimeApi surface.
class MatterRuntimeApi {
public:
    virtual ~MatterRuntimeApi() = default;

    virtual uint32_t next_operation_request_id() noexcept = 0;
    virtual CommandSubmitStatus post_device_power_request(
        uint32_t correlation_id,
        uint16_t short_addr,
        bool desired_power_on,
        uint32_t issued_at_ms) noexcept = 0;
    virtual bool build_matter_bridge_snapshot(MatterBridgeSnapshot* out) const noexcept = 0;
};

}  // namespace service
