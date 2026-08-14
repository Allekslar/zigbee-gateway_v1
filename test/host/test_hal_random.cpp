/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "hal_random.h"

int main() {
    // Rejects null/zero-length arguments.
    assert(hal_random_fill_bytes(nullptr, 4U) == HAL_RANDOM_STATUS_INVALID_ARG);
    uint8_t single_byte = 0U;
    assert(hal_random_fill_bytes(&single_byte, 0U) == HAL_RANDOM_STATUS_INVALID_ARG);

    // Fills the requested number of bytes.
    uint8_t buffer_a[16]{};
    assert(hal_random_fill_bytes(buffer_a, sizeof(buffer_a)) == HAL_RANDOM_STATUS_OK);

    // Two consecutive calls produce different output -- proves the host
    // PRNG (see hal_random.c -- NOT cryptographically secure, host-only)
    // actually varies rather than returning a fixed/zeroed pattern.
    // Astronomically unlikely to spuriously fail for a 16-byte buffer.
    uint8_t buffer_b[16]{};
    assert(hal_random_fill_bytes(buffer_b, sizeof(buffer_b)) == HAL_RANDOM_STATUS_OK);
    assert(std::memcmp(buffer_a, buffer_b, sizeof(buffer_a)) != 0);

    // A single-byte fill still succeeds (exercises the len==1 boundary).
    uint8_t tiny[1]{};
    assert(hal_random_fill_bytes(tiny, sizeof(tiny)) == HAL_RANDOM_STATUS_OK);

    return 0;
}
