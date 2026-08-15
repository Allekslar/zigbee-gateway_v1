/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "capability.hpp"

int main() {
    // Plan #18's exact token spelling, in enum order.
    assert(std::strcmp(service::capability_token(service::Capability::kReadStatus), "read_status") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kControlDevice), "control_device") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kManageNetwork), "manage_network") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kCommissionDevice), "commission_device") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kRemoveDevice), "remove_device") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kFirmwareAdmin), "firmware_admin") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kRcpAdmin), "rcp_admin") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kSecurityAdmin), "security_admin") == 0);
    assert(std::strcmp(service::capability_token(service::Capability::kFactoryReset), "factory_reset") == 0);
    assert(std::strcmp(service::capability_token(static_cast<service::Capability>(99)), "unknown") == 0);

    // Neither ota nor rcp available: firmware_admin/rcp_admin withheld,
    // the other 7 always granted.
    {
        service::RuntimeCapabilities caps{};
        service::Capability granted[service::kCapabilityCount]{};
        const uint32_t count = service::granted_capabilities(caps, granted, service::kCapabilityCount);
        assert(count == 7U);
        for (uint32_t i = 0U; i < count; ++i) {
            assert(granted[i] != service::Capability::kFirmwareAdmin);
            assert(granted[i] != service::Capability::kRcpAdmin);
        }
    }

    // Both available: all 9 granted, in declared order.
    {
        service::RuntimeCapabilities caps{};
        caps.ota_available = true;
        caps.rcp_update_available = true;
        service::Capability granted[service::kCapabilityCount]{};
        const uint32_t count = service::granted_capabilities(caps, granted, service::kCapabilityCount);
        assert(count == service::kCapabilityCount);
        assert(granted[0] == service::Capability::kReadStatus);
        assert(granted[service::kCapabilityCount - 1U] == service::Capability::kFactoryReset);
        bool saw_firmware_admin = false;
        bool saw_rcp_admin = false;
        for (uint32_t i = 0U; i < count; ++i) {
            saw_firmware_admin = saw_firmware_admin || granted[i] == service::Capability::kFirmwareAdmin;
            saw_rcp_admin = saw_rcp_admin || granted[i] == service::Capability::kRcpAdmin;
        }
        assert(saw_firmware_admin);
        assert(saw_rcp_admin);
    }

    // Undersized output buffer: never writes past out_capacity, returns
    // the number actually written.
    {
        service::RuntimeCapabilities caps{};
        caps.ota_available = true;
        caps.rcp_update_available = true;
        service::Capability granted[3]{};
        const uint32_t count = service::granted_capabilities(caps, granted, 3U);
        assert(count == 3U);
        assert(granted[0] == service::Capability::kReadStatus);
        assert(granted[1] == service::Capability::kControlDevice);
        assert(granted[2] == service::Capability::kManageNetwork);
    }

    // Null out pointer: rejected, not a crash.
    assert(service::granted_capabilities(service::RuntimeCapabilities{}, nullptr, 9U) == 0U);

    return 0;
}
