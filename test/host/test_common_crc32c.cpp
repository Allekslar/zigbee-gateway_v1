/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "crc32c.hpp"

int main() {
    // Well-known CRC-32C test vector.
    const char* check_input = "123456789";
    assert(common::crc32c(check_input, std::strlen(check_input)) == 0xE3069283U);

    // Empty input has a defined, non-crashing result.
    assert(common::crc32c("", 0) == 0x00000000U);

    // Deterministic and sensitive to every byte.
    const uint8_t a[4] = {1, 2, 3, 4};
    const uint8_t b[4] = {1, 2, 3, 5};
    assert(common::crc32c(a, sizeof(a)) == common::crc32c(a, sizeof(a)));
    assert(common::crc32c(a, sizeof(a)) != common::crc32c(b, sizeof(b)));

    // Null pointer is handled without crashing.
    assert(common::crc32c(nullptr, 10) == 0U);

    return 0;
}
