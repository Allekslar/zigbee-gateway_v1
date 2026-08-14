/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "secure_storage_port.hpp"

namespace service {

// Plan S6 "Provisioning and credentials" #5: "Enrollment creates a
// PBKDF2-HMAC-SHA256 admin password verifier with a per-device random
// 128-bit salt. Calibrate iterations to 250-500 ms on ESP32-C6 with a
// hard minimum of 50,000; store the selected iteration count with the
// verifier. Never store or log plaintext password."
//
// ESP_PLATFORM: PBKDF2-HMAC-SHA256 is computed via the real, non-
// deprecated mbedtls entry point `mbedtls_pkcs5_pbkdf2_hmac_ext()`
// (`mbedtls/pkcs5.h`, confirmed signature against the real header
// inside `espressif/idf:release-v5.5` before writing this declaration;
// declared unconditionally, unlike the PBES2 functions in the same
// header which are guarded behind MBEDTLS_ASN1_PARSE_C/MBEDTLS_CIPHER_C).
//
// Host builds (FD-22 "documented unmet product requirement" carve-out):
// `zgw-host-tools:s0` has no crypto library development headers at all
// (neither mbedtls nor OpenSSL -- confirmed by direct recon: no
// `mbedtls*` headers anywhere on the image, and while the OpenSSL
// *runtime* .so files are present, no `openssl/*.h` headers or
// `libssl-dev` package are installed, and no Dockerfile exists in this
// repository to rebuild that image with them added). Host builds
// therefore use a self-contained SHA-256/HMAC-SHA256/PBKDF2-HMAC-SHA256
// implementation private to admin_verifier.cpp, validated byte-for-byte
// against Python's hashlib.sha256/hmac/pbkdf2_hmac before being trusted
// (see the sub-slice's evidence file for the exact vectors) -- this
// mirrors hal_random.c's already-established ESP_PLATFORM-vs-host split
// pattern. This host implementation is NEVER compiled into a production
// (ESP_PLATFORM) build.

inline constexpr uint32_t kAdminVerifierSaltBytes = 16U;  // 128 bits, plan #5
inline constexpr uint32_t kAdminVerifierHashBytes = 32U;  // SHA-256 digest size
inline constexpr uint32_t kAdminVerifierMinIterations = 50000U;  // plan #5 hard minimum
inline constexpr uint32_t kAdminVerifierCalibrationTargetMinMs = 250U;  // plan #5
inline constexpr uint32_t kAdminVerifierCalibrationTargetMaxMs = 500U;  // plan #5

// Fixed-layout verifier record. Never reinterpret_cast this struct
// directly to/from a storage blob (persisted_device_state.hpp's own
// established convention) -- see serialize_admin_verifier_record() /
// deserialize_admin_verifier_record() below for the explicit,
// endian-defined wire format.
struct AdminVerifierRecord {
    uint8_t salt[kAdminVerifierSaltBytes]{};
    uint8_t hash[kAdminVerifierHashBytes]{};
    uint32_t iterations{0};
};

// Wire format: salt (16 bytes) || hash (32 bytes) || iterations (4
// bytes, little-endian) = 52 bytes total, matching
// nvs_namespace_registry.cpp's kAdminVerifier "admin_verifier" key.
inline constexpr uint32_t kAdminVerifierRecordBytes =
    kAdminVerifierSaltBytes + kAdminVerifierHashBytes + 4U;

void serialize_admin_verifier_record(const AdminVerifierRecord& record, uint8_t out[kAdminVerifierRecordBytes]) noexcept;

// Returns false (record left default-constructed) if `bytes_len` !=
// kAdminVerifierRecordBytes -- the schema-validity check
// get_stored_admin_verifier() feeds into
// secure_storage_downgrade_to_corrupt_if_invalid().
bool deserialize_admin_verifier_record(
    const uint8_t* bytes, uint32_t bytes_len, AdminVerifierRecord* out) noexcept;

// Runs one real PBKDF2-HMAC-SHA256 derivation of `iterations` rounds
// against a fixed, non-secret dummy password/salt (timing calibration
// only -- never a real credential) and returns how long it took, in
// milliseconds, via hal_time_now_ms().
uint32_t measure_pbkdf2_duration_ms(uint32_t iterations) noexcept;

// Starts at kAdminVerifierMinIterations and scales proportionally
// against measure_pbkdf2_duration_ms() until the measured duration
// falls inside [target_min_ms, target_max_ms] or a bounded retry count
// is exhausted (never loops unboundedly). Never returns a value below
// kAdminVerifierMinIterations regardless of how fast the measured
// device is.
uint32_t calibrate_pbkdf2_iterations(uint32_t target_min_ms, uint32_t target_max_ms) noexcept;

// Draws a fresh 128-bit salt from hal_random_fill_bytes(), calibrates
// the iteration count, and derives the verifier hash from `password`.
// `password` must be a non-empty, null-terminated string. Returns false
// (out untouched) on a null argument, empty password, or RNG failure.
// Never logs or otherwise retains `password` beyond this call's stack.
bool create_admin_verifier(const char* password, AdminVerifierRecord* out) noexcept;

// Re-derives the PBKDF2-HMAC-SHA256 hash from `password` using
// `record`'s stored salt and iteration count, then compares it against
// `record.hash` in constant time (timing-attack resistance -- the
// comparison never short-circuits on the first mismatched byte).
// Returns false for a null/empty password or a stored `record.iterations`
// below kAdminVerifierMinIterations (a corrupt or tampered record must
// never be trusted to gate its own verification cost).
bool verify_admin_password(const char* password, const AdminVerifierRecord& record) noexcept;

// Typed storage accessors over secure_storage_get_blob/set_blob
// (Section 2.8/2.9), scoped to NvsNamespaceId::kAdminVerifier's
// "admin_verifier" key (nvs_namespace_registry.cpp). Applies the fixed-
// length schema check via secure_storage_downgrade_to_corrupt_if_invalid()
// on read.
SecureStorageStatus get_stored_admin_verifier(AdminVerifierRecord* out) noexcept;
SecureStorageWriteResult set_stored_admin_verifier(const AdminVerifierRecord& record) noexcept;

// True only if get_stored_admin_verifier() reports kAvailable -- the
// "first-boot policy" trigger plan #3's commissioning window and the
// plan's own Migration/compatibility text ("Upgrade boots into
// restricted migration mode if no admin credential exists") both
// consume. Any non-available status (including kCorrupt/kUnavailable)
// is treated as "no credential" -- fail closed into the more-restricted
// state, never the reverse.
bool admin_credential_exists() noexcept;

}  // namespace service
