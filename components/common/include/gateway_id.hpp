/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace common {

// Canonical gateway identity (plan FD-17, S4 slice only -- the TLS/mDNS/
// certificate parts of FD-17 belong to S6). GatewayId is the ESP32 factory
// base MAC (6 bytes), rendered as exactly 12 lowercase hexadecimal
// characters with no separators. It survives ordinary and factory reset
// and must never be derived from mutable network configuration (Wi-Fi/BT
// interface MAC, hostname, Zigbee coordinator address).
//
// This class is a pure formatting/value type with no ESP-IDF dependency
// (INV-ARCH-01-equivalent boundary for the common component, enforced by
// INV-H007). The actual eFuse read lives at the HAL boundary
// (hal_identity.h); callers obtain the raw bytes there and construct a
// GatewayId from them.
class GatewayId {
public:
    static constexpr std::size_t kByteLength = 6U;
    static constexpr std::size_t kHexLength = kByteLength * 2U;

    constexpr GatewayId() noexcept = default;

    // `bytes` must already be the factory base MAC in its natural
    // big-endian byte order (as returned by hal_identity_get_factory_base_
    // mac()).
    explicit constexpr GatewayId(const std::array<uint8_t, kByteLength>& bytes) noexcept : bytes_(bytes) {}

    // Writes exactly kHexLength lowercase hex characters into `out` (no
    // null terminator is appended). `out_capacity` must be >= kHexLength.
    bool format(char* out, std::size_t out_capacity) const noexcept;

    // An all-zero value is reserved/invalid: it means the eFuse base MAC
    // was never programmed (or the HAL read failed and the caller passed
    // through a default-constructed value), not a real factory identity.
    bool valid() const noexcept;

    const std::array<uint8_t, kByteLength>& bytes() const noexcept { return bytes_; }

    // Not constexpr: std::array::operator== only became constexpr in C++20
    // and this project targets C++17 (matches core::DeviceId's rationale).
    friend bool operator==(const GatewayId& lhs, const GatewayId& rhs) noexcept { return lhs.bytes_ == rhs.bytes_; }
    friend bool operator!=(const GatewayId& lhs, const GatewayId& rhs) noexcept { return !(lhs == rhs); }

private:
    std::array<uint8_t, kByteLength> bytes_{};
};

}  // namespace common
