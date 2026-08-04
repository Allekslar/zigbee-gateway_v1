/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "crc32c.hpp"

namespace common {

namespace {

constexpr uint32_t kCrc32cPolynomial = 0x82F63B78U;  // reflected 0x1EDC6F41

uint32_t crc32c_update(uint32_t crc, uint8_t byte) noexcept {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
        crc = (crc >> 1U) ^ (kCrc32cPolynomial & mask);
    }
    return crc;
}

}  // namespace

uint32_t crc32c(const void* data, std::size_t len) noexcept {
    if (data == nullptr) {
        return 0U;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        crc = crc32c_update(crc, bytes[i]);
    }
    return crc ^ 0xFFFFFFFFU;
}

}  // namespace common
