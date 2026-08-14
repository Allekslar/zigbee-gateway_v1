/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "base32.hpp"

namespace common {

namespace {
constexpr char kAlphabet[33] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
}  // namespace

bool base32_encode(const uint8_t* input, std::size_t input_len, char* out, std::size_t out_capacity) noexcept {
    if (out == nullptr) {
        return false;
    }
    if (input_len > 0 && input == nullptr) {
        return false;
    }

    const std::size_t required_len = base32_encoded_length(input_len);
    if (out_capacity < required_len + 1U) {
        return false;
    }

    std::size_t bit_buffer = 0;
    int bits_in_buffer = 0;
    std::size_t out_index = 0;

    for (std::size_t i = 0; i < input_len; ++i) {
        bit_buffer = (bit_buffer << 8) | input[i];
        bits_in_buffer += 8;
        while (bits_in_buffer >= 5) {
            bits_in_buffer -= 5;
            out[out_index++] = kAlphabet[(bit_buffer >> bits_in_buffer) & 0x1FU];
        }
    }
    if (bits_in_buffer > 0) {
        out[out_index++] = kAlphabet[(bit_buffer << (5 - bits_in_buffer)) & 0x1FU];
    }

    out[out_index] = '\0';
    return true;
}

}  // namespace common
