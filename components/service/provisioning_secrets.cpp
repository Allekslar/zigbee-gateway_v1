/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "provisioning_secrets.hpp"

#include <cstring>

#include "base32.hpp"
#include "hal_random.h"
#include "security_bounds.hpp"

namespace service {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

// Number of random bytes that encode to exactly
// SecurityBounds::kProvisioningPassphraseBase32Chars Base32 characters
// with no padding: 10 bytes * 8 bits = 80 bits = 16 * 5 bits exactly.
constexpr uint32_t kProvisioningPassphraseRandomBytes = 10U;

// How many trailing hex characters of the GatewayId form the shared
// "<prefix>-<last6>" suffix -- both plan #4's AP SSID and plan #9's
// production mDNS host use exactly this same convention.
constexpr std::size_t kGatewaySuffixHexChars = 6U;

// Shared by build_provisioning_ap_ssid() (plan #4) and
// build_gateway_mdns_host()'s production branch (plan #9) -- both build
// "<prefix>-<last 6 lowercase hex characters of gateway_id>", differing
// only in which prefix string and which GatewayId validity/error
// behavior their own callers need.
bool build_prefixed_gateway_suffix_name(
    const common::GatewayId& gateway_id, const char* prefix, char* out, uint32_t out_capacity) noexcept {
    if (out == nullptr || prefix == nullptr || prefix[0] == '\0' || !gateway_id.valid()) {
        return false;
    }

    char gateway_hex[common::GatewayId::kHexLength]{};
    if (!gateway_id.format(gateway_hex, sizeof(gateway_hex))) {
        return false;
    }

    const std::size_t prefix_len = std::strlen(prefix);
    const std::size_t required_len = prefix_len + 1U /* '-' */ + kGatewaySuffixHexChars + 1U /* '\0' */;
    if (out_capacity < required_len) {
        return false;
    }

    std::memcpy(out, prefix, prefix_len);
    out[prefix_len] = '-';
    std::memcpy(
        out + prefix_len + 1U, gateway_hex + (common::GatewayId::kHexLength - kGatewaySuffixHexChars),
        kGatewaySuffixHexChars);
    out[prefix_len + 1U + kGatewaySuffixHexChars] = '\0';
    return true;
}

}  // namespace

bool generate_provisioning_passphrase(char* out, uint32_t out_capacity) noexcept {
    static_assert(
        common::base32_encoded_length(kProvisioningPassphraseRandomBytes) ==
            SecurityBounds::kProvisioningPassphraseBase32Chars,
        "provisioning passphrase random-byte count must encode to exactly the plan's approved Base32 length");

    if (out == nullptr || out_capacity < SecurityBounds::kProvisioningPassphraseBase32Chars + 1U) {
        return false;
    }

    uint8_t random_bytes[kProvisioningPassphraseRandomBytes]{};
    if (hal_random_fill_bytes(random_bytes, sizeof(random_bytes)) != HAL_RANDOM_STATUS_OK) {
        return false;
    }

    return common::base32_encode(random_bytes, sizeof(random_bytes), out, out_capacity);
}

bool generate_random_secret_hex(uint32_t byte_len, char* out, uint32_t out_capacity) noexcept {
    if (out == nullptr || byte_len == 0U) {
        return false;
    }
    const uint32_t required_capacity = (byte_len * 2U) + 1U;
    if (out_capacity < required_capacity) {
        return false;
    }

    // Bounded stack buffer -- this project's real S6 use cases (session
    // IDs, CSRF tokens) are at most a few dozen bytes; a caller asking
    // for more than this is almost certainly a mistake, rejected
    // defensively rather than risking an unbounded stack allocation.
    constexpr uint32_t kMaxRandomBytes = 64U;
    if (byte_len > kMaxRandomBytes) {
        return false;
    }

    uint8_t random_bytes[kMaxRandomBytes]{};
    if (hal_random_fill_bytes(random_bytes, byte_len) != HAL_RANDOM_STATUS_OK) {
        return false;
    }

    for (uint32_t i = 0; i < byte_len; ++i) {
        out[i * 2U] = kHexDigits[(random_bytes[i] >> 4) & 0x0FU];
        out[i * 2U + 1U] = kHexDigits[random_bytes[i] & 0x0FU];
    }
    out[byte_len * 2U] = '\0';
    return true;
}

bool build_provisioning_ap_ssid(
    const common::GatewayId& gateway_id, const char* prefix, char* out, uint32_t out_capacity) noexcept {
    return build_prefixed_gateway_suffix_name(gateway_id, prefix, out, out_capacity);
}

bool build_gateway_mdns_host(const common::GatewayId& gateway_id, char* out, uint32_t out_capacity) noexcept {
#if defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
    // Plan #9, verbatim: "Derive production mDNS host exactly as
    // zigbee-gateway-<last6>.local" -- reuses the identical
    // "<prefix>-<last6-hex>" convention plan #4's AP SSID suffix already
    // established (build_prefixed_gateway_suffix_name() above). This
    // builds only the hostname portion before ".local" -- hal_mdns.h's
    // mdns_hostname_set() and mDNS resolvers themselves append that
    // suffix, matching how this project's pre-existing plain
    // "zigbee-gateway" hostname was never itself suffixed with ".local"
    // either.
    return build_prefixed_gateway_suffix_name(gateway_id, kGatewayHostNamePrefix, out, out_capacity);
#else
    // Development: the plain, unsuffixed prefix -- a fixed, predictable
    // hostname is more convenient for bench-testing a single device on a
    // LAN with no other zgw devices, and this is not itself a
    // security-relevant distinction (a development build already serves
    // plain HTTP unconditionally -- see web_server.cpp's own plan #7
    // branch -- so per-device hostname collision is a convenience
    // concern here, not a trust concern the way it is in production).
    (void)gateway_id;
    if (out == nullptr) {
        return false;
    }
    const std::size_t len = std::strlen(kGatewayHostNamePrefix);
    if (out_capacity < len + 1U) {
        return false;
    }
    std::memcpy(out, kGatewayHostNamePrefix, len + 1U);
    return true;
#endif
}

bool build_gateway_uri_san(const common::GatewayId& gateway_id, char* out, uint32_t out_capacity) noexcept {
    constexpr char kUriPrefix[] = "urn:zgw:";
    constexpr std::size_t kUriPrefixLen = sizeof(kUriPrefix) - 1U;  // excludes '\0'

    if (out == nullptr || !gateway_id.valid()) {
        return false;
    }

    char gateway_hex[common::GatewayId::kHexLength]{};
    if (!gateway_id.format(gateway_hex, sizeof(gateway_hex))) {
        return false;
    }

    const std::size_t required_len = kUriPrefixLen + common::GatewayId::kHexLength + 1U /* '\0' */;
    if (out_capacity < required_len) {
        return false;
    }

    std::memcpy(out, kUriPrefix, kUriPrefixLen);
    std::memcpy(out + kUriPrefixLen, gateway_hex, common::GatewayId::kHexLength);
    out[kUriPrefixLen + common::GatewayId::kHexLength] = '\0';
    return true;
}

}  // namespace service
