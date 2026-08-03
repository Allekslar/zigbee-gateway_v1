/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace core {

// Canonical durable device identity: a Zigbee EUI-64, stored most-significant
// byte first (canonical network order). This is the sole authoritative key
// for device state, config and external contracts; `short_addr` is always a
// mutable locator, never identity (FD-01). Trivial, fixed-size, no heap
// allocation, safe to compare/copy/persist byte-for-byte.
class DeviceId {
public:
    static constexpr std::size_t kByteLength = 8U;
    static constexpr std::size_t kHexLength = kByteLength * 2U;

    constexpr DeviceId() noexcept = default;

    // `bytes` must already be in canonical (most-significant-byte-first)
    // order; callers at the HAL boundary are responsible for normalizing any
    // stack-specific byte order before construction (FD-01/FD-02).
    explicit constexpr DeviceId(const std::array<uint8_t, kByteLength>& bytes) noexcept : bytes_(bytes) {}

    // Parses exactly 16 lowercase hexadecimal characters (no separators). On
    // failure (wrong length, non-hex, or non-canonical case) returns false
    // and leaves `out` unmodified.
    static bool parse(const char* text, std::size_t len, DeviceId* out) noexcept;

    // Writes exactly kHexLength lowercase hex characters into `out` (no null
    // terminator is appended). `out_capacity` must be >= kHexLength.
    bool format(char* out, std::size_t out_capacity) const noexcept;

    // All-zero and all-0xff values are reserved/invalid per FD-01.
    bool valid() const noexcept;

    const std::array<uint8_t, kByteLength>& bytes() const noexcept { return bytes_; }

    // Not constexpr: std::array::operator== only became constexpr in C++20
    // and this project targets C++17.
    friend bool operator==(const DeviceId& lhs, const DeviceId& rhs) noexcept { return lhs.bytes_ == rhs.bytes_; }
    friend bool operator!=(const DeviceId& lhs, const DeviceId& rhs) noexcept { return !(lhs == rhs); }

    // Bounded, hash-free lexicographic ordering (byte-wise, most-significant
    // byte first) so DeviceId can be used as a key in simple sorted/bounded
    // containers without pulling in std::hash specializations.
    friend constexpr bool operator<(const DeviceId& lhs, const DeviceId& rhs) noexcept {
        for (std::size_t i = 0; i < kByteLength; ++i) {
            if (lhs.bytes_[i] != rhs.bytes_[i]) {
                return lhs.bytes_[i] < rhs.bytes_[i];
            }
        }
        return false;
    }

private:
    std::array<uint8_t, kByteLength> bytes_{};
};

}  // namespace core
