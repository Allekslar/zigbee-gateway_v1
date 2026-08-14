/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_RANDOM_STATUS_OK = 0,
    HAL_RANDOM_STATUS_INVALID_ARG = -1,
} hal_random_status_t;

// Plan S6 "Provisioning and credentials" #4 ("its passphrase is exactly
// 16 cryptographically random Base32 characters") and #6 ("Generate
// session and CSRF secrets from hardware RNG").
//
// Fills `out` with `len` bytes of randomness.
//
// ESP_PLATFORM: wraps ESP-IDF's real `esp_fill_random()`
// (`esp_hw_support/include/esp_random.h`, confirmed against the real
// header inside `espressif/idf:release-v5.5` before writing this
// declaration). Its own documentation states the exact condition for
// true randomness: "If Wi-Fi or Bluetooth are enabled, this function
// returns true random numbers. In other situations, if true random
// numbers are required then consult the ESP-IDF Programming Guide...
// for necessary prerequisites." This project's provisioning-passphrase
// call site (`main/app_main.cpp`) generates the passphrase only after
// the Wi-Fi subsystem has already been brought up to start the
// provisioning AP itself, so this condition is satisfied there. A
// future caller needing this guarantee before Wi-Fi/BT are enabled must
// check the ESP-IDF Programming Guide's prerequisites itself -- this
// wrapper does not (and cannot) verify radio state on the caller's
// behalf.
//
// Host builds: **NOT cryptographically secure** -- see `hal_random.c`
// for the exact host mechanism (a plain, non-cryptographic PRNG, used
// only so host tests can observe "output actually varies" without a
// real hardware TRNG). No production code path ever executes the host
// branch; production always builds for `ESP_PLATFORM`.
hal_random_status_t hal_random_fill_bytes(uint8_t* out, uint32_t len);

#ifdef __cplusplus
}
#endif
