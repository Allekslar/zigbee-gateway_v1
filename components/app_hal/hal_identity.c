/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_identity.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_mac.h"
#endif

// Test-only override hooks (declared for external callers in
// hal_identity_test.h, gated by SERVICE_RUNTIME_TEST_HOOKS there). Defined
// unconditionally here, matching the hal_zigbee.c pattern, so this
// translation unit does not need to know about the test-hooks macro; on
// ESP_PLATFORM the setter is a no-op so no build profile can be used to
// spoof gateway identity.
void hal_identity_set_mock_base_mac(const uint8_t* mac);
void hal_identity_reset_mock_base_mac(void);

#ifndef ESP_PLATFORM
static const uint8_t kDefaultMockBaseMac[HAL_IDENTITY_BASE_MAC_LEN] = {0x00, 0x12, 0x4b, 0x00, 0x00, 0x01};
static uint8_t s_mock_base_mac[HAL_IDENTITY_BASE_MAC_LEN];
static int s_mock_base_mac_initialized = 0;
#endif

int hal_identity_get_factory_base_mac(uint8_t* out) {
    if (out == NULL) {
        return -1;
    }

#ifdef ESP_PLATFORM
    return (esp_efuse_mac_get_default(out) == ESP_OK) ? 0 : -1;
#else
    if (!s_mock_base_mac_initialized) {
        memcpy(s_mock_base_mac, kDefaultMockBaseMac, HAL_IDENTITY_BASE_MAC_LEN);
        s_mock_base_mac_initialized = 1;
    }
    memcpy(out, s_mock_base_mac, HAL_IDENTITY_BASE_MAC_LEN);
    return 0;
#endif
}

void hal_identity_set_mock_base_mac(const uint8_t* mac) {
#ifndef ESP_PLATFORM
    if (mac == NULL) {
        return;
    }
    memcpy(s_mock_base_mac, mac, HAL_IDENTITY_BASE_MAC_LEN);
    s_mock_base_mac_initialized = 1;
#else
    (void)mac;
#endif
}

void hal_identity_reset_mock_base_mac(void) {
#ifndef ESP_PLATFORM
    memcpy(s_mock_base_mac, kDefaultMockBaseMac, HAL_IDENTITY_BASE_MAC_LEN);
    s_mock_base_mac_initialized = 1;
#endif
}
