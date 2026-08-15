/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

// Plan S6 "Provisioning and credentials" #2: "Introduce a
// ProvisioningSecretProvider port: production adapter reads per-device
// manufacturing proof-of-possession material; development adapter may
// generate and print a one-time secret; production startup fails closed
// when material is absent."
//
// This is a distinct secret from the Wi-Fi provisioning-AP passphrase
// (provisioning_secrets.hpp's generate_provisioning_passphrase(), plan
// #4) -- that one authenticates joining the AP itself and is
// regenerated fresh every boot; this one is the per-device manufacturing
// proof-of-possession material plan #2 names, sourced from S5's
// manufacturing_provisioning_get/set_proof_of_possession()
// (tls_provisioning_storage_port.hpp) in production. Nothing in this
// repository consumes provisioning_secret_provider_get() yet -- the
// actual enrollment/PASE-style consumer that would use this material is
// a later S6 sub-slice, matching the established "port defined ahead of
// its full pipeline" discipline (see tls_provisioning_storage_port.hpp's
// own top-of-file comment for the same pattern in S5).
//
// Adapter selection is via CONFIG_ZGW_PRODUCTION_PROFILE (main/
// Kconfig.projbuild), not CONFIG_ZGW_PRODUCTION_BUILD -- the latter is a
// build-time-only CMake variable that never becomes a compiled
// CONFIG_ZGW_* macro (see this header's introducing evidence file for
// the recon that found this gap).

inline constexpr uint32_t kProvisioningSecretMaxBytes = 64U;

// Development-adapter-only: byte length of the ephemeral secret it
// generates (128 bits -- matches provisioning_secrets.hpp's own
// generate_random_secret_hex() byte-count convention for session/CSRF
// secrets).
inline constexpr uint32_t kProvisioningSecretDevBytes = 16U;

struct ProvisioningSecret {
    uint8_t bytes[kProvisioningSecretMaxBytes]{};
    uint32_t len{0};
};

enum class ProvisioningSecretStatus : uint8_t {
    kAvailable = 0,
    // Production: manufacturing_provisioning_get_proof_of_possession()
    // did not return SecureStorageStatus::kAvailable (not provisioned,
    // corrupt, or the storage subsystem itself is unavailable) -- plan
    // #2's own "production startup fails closed when material is
    // absent." Development: hal_random_fill_bytes() failed, or the
    // generated secret does not fit ProvisioningSecret::bytes (a
    // programming-error-only case given the fixed 16-byte dev length).
    kUnavailable = 1,
};

// Production (CONFIG_ZGW_PRODUCTION_PROFILE=y): reads the manufacturing
// proof-of-possession material via
// manufacturing_provisioning_get_proof_of_possession(); returns
// kUnavailable (out untouched) for any non-kAvailable
// SecureStorageStatus -- fails closed, never falls back to generating
// one.
//
// Development (CONFIG_ZGW_PRODUCTION_PROFILE unset, or host build):
// draws kProvisioningSecretDevBytes of fresh randomness from
// hal_random_fill_bytes() every call (never persisted -- "one-time"
// per plan #2's own wording) and logs it via ESP_LOGI so an installer
// can read it over the serial console, mirroring main/app_main.cpp's
// existing Wi-Fi AP passphrase delivery posture (this device has no
// display, only UART -- see that call site's own comment for the same
// reasoning).
ProvisioningSecretStatus provisioning_secret_provider_get(ProvisioningSecret* out) noexcept;

// Plan S6 "Authorization and physical presence" #17's `POST
// /api/v1/provisioning/enroll` ("proof of possession") and #22's factory-
// reset "fresh manufacturing PoP challenge" both need to compare a
// caller-submitted candidate against `expected` -- this is that
// comparison, constant-time (never short-circuits on the first
// mismatched byte), matching admin_verifier.cpp's own password-hash
// comparison discipline and session_security_policy.cpp's CSRF-token
// comparison discipline (each module owns its own small compare rather
// than sharing one, the established convention here). False (not a
// match) if the lengths differ at all -- a length mismatch is decided
// before entering the constant-time loop, matching how a HMAC/hash
// comparison of two different-length inputs is never meaningfully
// "close" in the first place.
bool provisioning_secret_matches(
    const ProvisioningSecret& expected, const uint8_t* candidate_bytes, uint32_t candidate_len) noexcept;

}  // namespace service
