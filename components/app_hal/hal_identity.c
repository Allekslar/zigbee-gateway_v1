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
    // esp_efuse_mac_get_default() is documented (esp_mac.h) to need an
    // 8-byte buffer, not 6, on any SOC_IEEE802154_SUPPORTED target
    // (ESP32-C6, ESP32-H2): it fills the 6-byte factory MAC and then
    // *overwrites bytes [3..7]* with an EUI-64 expansion (inserting the
    // fixed MAC_EXT eFuse bytes "ff:fe" at [3..4] and shifting the real
    // NIC bytes to [5..7]) for IEEE 802.15.4 use. Passing our plain
    // HAL_IDENTITY_BASE_MAC_LEN==6 buffer here overran it by 2 bytes and
    // silently corrupted the "factory base MAC" this function promises --
    // confirmed on real ESP32-C6 hardware (2026-08-14 HIL session): every
    // boot computed GatewayId low bytes as ff:fe:9f (the EUI-64 padding
    // plus one surviving real byte) instead of the true base MAC's real
    // 9f:00:20, so every derived name (provisioning AP SSID, production
    // mDNS host) used the wrong, low-entropy suffix. esp_read_mac() with
    // ESP_MAC_EFUSE_FACTORY reads the exact same eFuse field but always
    // into a plain 6-byte MAC-48, with no IEEE802154 expansion -- see
    // esp_mac.h's enum comment ("MAC_FACTORY eFuse ... (6 bytes)", no
    // conditional 8-byte note, unlike ESP_MAC_BASE/_DEFAULT/_CUSTOM).
    return (esp_read_mac(out, ESP_MAC_EFUSE_FACTORY) == ESP_OK) ? 0 : -1;
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
