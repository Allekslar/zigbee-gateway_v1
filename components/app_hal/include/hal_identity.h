/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_IDENTITY_BASE_MAC_LEN 6U

// Reads the ESP32 factory base MAC (eFuse BLK0 via esp_efuse_mac_get_
// default()) into `out`, which must have at least HAL_IDENTITY_BASE_MAC_LEN
// bytes of capacity. This is the "factory base MAC" per plan FD-17: stable
// across reboot and factory reset, independent of any mutable Wi-Fi/BT
// interface-derived MAC or network configuration. Returns 0 on success,
// negative on failure (`out` untouched).
//
// On host/test builds (no ESP_PLATFORM), returns a fixed deterministic
// value unless overridden via hal_identity_test.h.
int hal_identity_get_factory_base_mac(uint8_t* out);

#ifdef __cplusplus
}
#endif
