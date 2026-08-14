/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "gateway_id.hpp"

namespace service {

// Plan S6 "Provisioning and credentials" #1, #4, #6, and "HTTPS and
// sessions" #9:
// #1: "Remove kProvisioningApPassword = '12345678' and every
//     shared/default production secret."
// #4: "Provisioning AP SSID includes a non-secret gateway suffix; its
//     passphrase is exactly 16 cryptographically random Base32
//     characters and uses WPA2/WPA3 settings supported by target."
// #6: "Generate session and CSRF secrets from hardware RNG."
// #9: "Derive production mDNS host exactly as
//     `zigbee-gateway-<last6>.local` and advertise only `https://`."
//
// The first three generators sit on top of hal_random.h (the hardware
// RNG primitive) and common::base32_encode (pure formatting) -- this
// file adds no cryptographic logic of its own, only the specific byte
// counts and formats plan #4/#6 name. #6's actual session/CSRF
// *subsystem* is now session_store.hpp/session_security_policy.hpp
// (S6-session-store-completion.json) -- generate_random_secret_hex() is
// the reusable primitive that subsystem calls, not the subsystem itself.
// #9's build_gateway_mdns_host() reuses the exact same "<prefix>-<last6>"
// naming convention plan #4's SSID suffix already established via a
// shared private helper (provisioning_secrets.cpp).

// Generates the 16-character provisioning AP passphrase (plan #4) into
// `out`, which must be at least
// `SecurityBounds::kProvisioningPassphraseBase32Chars + 1` bytes.
// Draws exactly 10 random bytes from hal_random_fill_bytes() -- 10 bytes
// = 80 bits = exactly 16 Base32 characters with no padding, matching
// common::base32_encoded_length(10) == 16. Returns false (out untouched)
// if `out_capacity` is too small or the underlying RNG call fails.
bool generate_provisioning_passphrase(char* out, uint32_t out_capacity) noexcept;

// Generates a lowercase-hex-encoded random secret of `byte_len` random
// bytes (2 * byte_len hex characters + a null terminator) into `out` --
// the reusable primitive plan #6 names for session/CSRF secrets. Returns
// false (out untouched) if `out_capacity` is too small
// (< 2 * byte_len + 1) or the underlying RNG call fails.
bool generate_random_secret_hex(uint32_t byte_len, char* out, uint32_t out_capacity) noexcept;

// Builds the provisioning AP SSID (plan #4: "includes a non-secret
// gateway suffix") as "<prefix>-<last 6 lowercase hex characters of
// gateway_id>" -- the same last-6-hex-characters suffix convention plan
// #9 names for the production mDNS host ("zigbee-gateway-<last6>.local"),
// reused here for naming consistency (build_gateway_mdns_host() below is
// that later, now-implemented consumer). `gateway_id` must be valid
// (`GatewayId::valid()`); `prefix` must be non-null and non-empty.
// Returns false (out untouched) if `out_capacity` is too small,
// `gateway_id` is invalid, or `prefix` is null/empty.
bool build_provisioning_ap_ssid(
    const common::GatewayId& gateway_id, const char* prefix, char* out, uint32_t out_capacity) noexcept;

// The single canonical "zigbee-gateway" prefix string plan #9 names --
// shared by build_gateway_mdns_host() below and this project's own
// provisioning AP SSID call site (main/app_main.cpp), so both stay in
// sync by construction rather than by two independently-maintained
// literal strings.
inline constexpr const char* kGatewayHostNamePrefix = "zigbee-gateway";

// Plan S6 "HTTPS and sessions" #9: "Derive production mDNS host exactly
// as `zigbee-gateway-<last6>.local` and advertise only `https://`."
//
// Builds this gateway's mDNS hostname -- the portion before ".local",
// which hal_mdns.h's mdns_hostname_set() and mDNS resolvers themselves
// append, matching how this project's hostname was never itself
// suffixed with ".local" even before this function existed.
//
// Production (`CONFIG_ZGW_PRODUCTION_PROFILE=y`): exactly
// "<kGatewayHostNamePrefix>-<last6-hex-of-gateway_id>", reusing
// build_provisioning_ap_ssid()'s identical "<prefix>-<last6>" convention.
// `gateway_id` must be valid in this branch.
//
// Development (`CONFIG_ZGW_PRODUCTION_PROFILE` unset, or a host build):
// the plain, unsuffixed `kGatewayHostNamePrefix` -- a fixed, predictable
// hostname is more convenient for bench-testing a single device, and
// this is not itself a security-relevant distinction (a development
// build already serves plain HTTP unconditionally, see web_server.cpp's
// own plan #7 branch). `gateway_id` is not read at all in this branch.
//
// Returns false (out untouched) if `out_capacity` is too small, or (the
// production branch only) `gateway_id` is invalid.
bool build_gateway_mdns_host(const common::GatewayId& gateway_id, char* out, uint32_t out_capacity) noexcept;

// Plan S6 "HTTPS and sessions" #10, verbatim fragment: "...contain both
// the exact mDNS DNS SAN and URI SAN `urn:zgw:<gateway_id>`."
//
// Builds exactly "urn:zgw:<12-lowercase-hex-chars-of-gateway_id>" -- the
// expected certificate URI Subject Alternative Name
// `tls_certificate_validator.hpp` checks for (plan #10/#11), matching
// `GatewayId::format()`'s own lowercase-hex convention. `gateway_id`
// must be valid. Returns false (out untouched) if `out_capacity` is too
// small or `gateway_id` is invalid. Pure string formatting -- no crypto
// or storage dependency of its own, kept host-testable exactly like this
// file's other two builders even though its one real consumer
// (`tls_certificate_validator.cpp`) is ESP_PLATFORM-only.
bool build_gateway_uri_san(const common::GatewayId& gateway_id, char* out, uint32_t out_capacity) noexcept;

}  // namespace service
