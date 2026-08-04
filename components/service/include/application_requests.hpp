/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "config_manager.hpp"
#include "service_public_types.hpp"

namespace service {

struct DevicePowerCommandRequest {
    uint32_t correlation_id{0};
    uint16_t short_addr{kUnknownShortAddr};
    bool desired_power_on{false};
    uint32_t issued_at_ms{0};
};

// A reporting-profile write as parsed from a Web/MQTT request. short_addr is
// the transport-facing locator used by the client; it is resolved to a
// core::DeviceId (FD-01) by the caller (which owns the locator registry via
// ServiceRuntime) before the embedded profile is durably written -- see
// ServiceRuntimeApi::resolve_device_id_for_short_addr.
struct ReportingProfileWriteRequest {
    uint16_t short_addr{kUnknownShortAddr};
    ConfigManager::ReportingProfile profile{};
};

}  // namespace service
