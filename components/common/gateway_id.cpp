/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "gateway_id.hpp"

namespace common {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

}  // namespace

bool GatewayId::format(char* out, std::size_t out_capacity) const noexcept {
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

bool GatewayId::valid() const noexcept {
    for (const uint8_t byte_value : bytes_) {
        if (byte_value != 0x00U) {
            return true;
        }
    }
    return false;
}

}  // namespace common
