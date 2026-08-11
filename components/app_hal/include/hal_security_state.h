/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Plan S5 required change #12 (docs/implementation/PRODUCTION_HARDENING_PLAN.md,
// "Encrypted storage foundation"): "Ensure no new production secret is
// written before NVS Encryption and required key protection are verified
// at runtime."
//
// Returns whether hardware Flash Encryption -- and, per this project's
// approved production Kconfig profile (`CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y`,
// docs/security/PRODUCTION_HARDENING.md Section 2.1: "NVS encryption key
// sourced from the Flash Encryption key rather than a separate HMAC eFuse
// key block"), NVS Encryption's own key protection along with it -- is
// verified ACTIVE right now, not just requested at build time.
//
// ESP_PLATFORM: wraps ESP-IDF's own real `esp_flash_encryption_enabled()`
// (`bootloader_support/include/esp_flash_encrypt.h`, confirmed against the
// real espressif/idf:release-v5.5 Docker image, not assumed from memory --
// same verification discipline as every other Kconfig/API symbol in this
// stage). That function reads the actual eFuse-backed state directly, not
// the Kconfig profile that happened to be compiled in -- catching a real
// drift case this project's own verifier (scripts/verify_production_security_profile.py)
// cannot: a device flashed with a production-profile build whose Flash
// Encryption eFuse bit was never actually burned.
//
// Host builds: always returns false unless overridden via
// hal_security_state_test.h (SERVICE_RUNTIME_TEST_HOOKS) -- there is no
// real flash/eFuse to check on a host build, and a host build must never
// claim encryption is verified active by default (the same fail-closed
// default this function's only caller, secure_storage_port.cpp's write
// gate, depends on).
bool hal_security_state_flash_encryption_enabled(void);

#ifdef __cplusplus
}
#endif
