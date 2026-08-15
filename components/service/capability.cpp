/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "capability.hpp"

namespace service {

const char* capability_token(Capability capability) noexcept {
    switch (capability) {
        case Capability::kReadStatus:
            return "read_status";
        case Capability::kControlDevice:
            return "control_device";
        case Capability::kManageNetwork:
            return "manage_network";
        case Capability::kCommissionDevice:
            return "commission_device";
        case Capability::kRemoveDevice:
            return "remove_device";
        case Capability::kFirmwareAdmin:
            return "firmware_admin";
        case Capability::kRcpAdmin:
            return "rcp_admin";
        case Capability::kSecurityAdmin:
            return "security_admin";
        case Capability::kFactoryReset:
            return "factory_reset";
        default:
            return "unknown";
    }
}

uint32_t granted_capabilities(
    const RuntimeCapabilities& runtime_caps, Capability* out, uint32_t out_capacity) noexcept {
    if (out == nullptr) {
        return 0U;
    }

    uint32_t count = 0U;
    const auto append = [&](Capability capability) {
        if (count < out_capacity) {
            out[count] = capability;
        }
        ++count;
    };

    append(Capability::kReadStatus);
    append(Capability::kControlDevice);
    append(Capability::kManageNetwork);
    append(Capability::kCommissionDevice);
    append(Capability::kRemoveDevice);
    if (runtime_caps.ota_available) {
        append(Capability::kFirmwareAdmin);
    }
    if (runtime_caps.rcp_update_available) {
        append(Capability::kRcpAdmin);
    }
    append(Capability::kSecurityAdmin);
    append(Capability::kFactoryReset);

    return count <= out_capacity ? count : out_capacity;
}

}  // namespace service
