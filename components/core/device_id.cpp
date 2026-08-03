/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "device_id.hpp"

namespace core {

namespace {

bool hex_nibble(char c, uint8_t* out) noexcept {
    if (c >= '0' && c <= '9') {
        *out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    // Uppercase hex is intentionally rejected: FD-01 defines the canonical
    // text form as exactly lowercase, and this parser only accepts the
    // canonical form. Callers that need to accept uppercase input must
    // normalize it before calling parse().
    return false;
}

constexpr char kHexDigits[] = "0123456789abcdef";

}  // namespace

bool DeviceId::parse(const char* text, std::size_t len, DeviceId* out) noexcept {
    if (text == nullptr || out == nullptr || len != kHexLength) {
        return false;
    }

    std::array<uint8_t, kByteLength> bytes{};
    for (std::size_t byte_index = 0; byte_index < kByteLength; ++byte_index) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!hex_nibble(text[byte_index * 2U], &high) || !hex_nibble(text[byte_index * 2U + 1U], &low)) {
            return false;
        }
        bytes[byte_index] = static_cast<uint8_t>((high << 4U) | low);
    }

    *out = DeviceId(bytes);
    return true;
}

bool DeviceId::format(char* out, std::size_t out_capacity) const noexcept {
    if (out == nullptr || out_capacity < kHexLength) {
        return false;
    }

    for (std::size_t byte_index = 0; byte_index < kByteLength; ++byte_index) {
        const uint8_t byte_value = bytes_[byte_index];
        out[byte_index * 2U] = kHexDigits[(byte_value >> 4U) & 0x0FU];
        out[byte_index * 2U + 1U] = kHexDigits[byte_value & 0x0FU];
    }
    return true;
}

bool DeviceId::valid() const noexcept {
    bool all_zero = true;
    bool all_ff = true;
    for (const uint8_t byte_value : bytes_) {
        if (byte_value != 0x00U) {
            all_zero = false;
        }
        if (byte_value != 0xFFU) {
            all_ff = false;
        }
    }
    return !all_zero && !all_ff;
}

}  // namespace core
