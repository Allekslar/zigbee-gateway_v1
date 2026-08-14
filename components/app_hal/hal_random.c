/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_random.h"

#ifdef ESP_PLATFORM
#include "esp_random.h"
#else
#include <stdlib.h>
#include <time.h>
#endif

#ifndef ESP_PLATFORM
static int s_host_prng_seeded = 0;
#endif

hal_random_status_t hal_random_fill_bytes(uint8_t* out, uint32_t len) {
    if (out == 0 || len == 0U) {
        return HAL_RANDOM_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    esp_fill_random(out, (size_t)len);
#else
    /* NOT cryptographically secure -- see hal_random.h. Seeded once per
     * process so repeated host test runs still see varying output
     * (unlike hal_identity.c's deliberately-fixed mock, callers here
     * want to observe actual variation, e.g. "two consecutive calls
     * differ"). */
    if (!s_host_prng_seeded) {
        srand((unsigned int)time(0));
        s_host_prng_seeded = 1;
    }
    for (uint32_t i = 0; i < len; ++i) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
#endif

    return HAL_RANDOM_STATUS_OK;
}
