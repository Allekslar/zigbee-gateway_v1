/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace common {

// Plan S6 "Provisioning and credentials" #4: "its passphrase is exactly
// 16 cryptographically random Base32 characters." Pure, no-ESP-IDF-
// dependency formatting utility (matching this component's existing
// GatewayId/crc32c boundary -- INV-H007-equivalent), independent of
// where the random bytes themselves came from (see hal_random.h for
// that).
//
// Unpadded RFC 4648 Base32 (uppercase alphabet "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
// no '=' padding characters -- this project only ever encodes byte counts
// that are already an exact multiple of 5 bytes, e.g. 10 bytes -> 16
// characters for the provisioning passphrase, so padding never applies;
// a non-multiple-of-5-bytes input is still encoded correctly, just
// without trailing '=' padding a decoder would otherwise expect).

// Returns the exact output length (excluding the null terminator)
// base32_encode() would produce for `input_len` bytes -- callers can use
// this to size a buffer precisely before calling.
constexpr std::size_t base32_encoded_length(std::size_t input_len) noexcept {
    return (input_len * 8 + 4) / 5;
}

// Encodes `input_len` bytes of `input` as unpadded Base32 into `out`
// (null-terminated on success). Returns false -- and leaves `out`
// untouched -- if `out_capacity` is smaller than
// `base32_encoded_length(input_len) + 1`. `input` and `out` must not be
// null unless `input_len` is 0.
bool base32_encode(const uint8_t* input, std::size_t input_len, char* out, std::size_t out_capacity) noexcept;

}  // namespace common
