/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "admin_verifier.hpp"

#include <cstring>

#include "hal_random.h"
#include "hal_time.h"
#include "nvs_namespace_registry.hpp"

#ifdef ESP_PLATFORM
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#else
#include <cstdint>
#endif

namespace service {

namespace {

constexpr const char* kAdminVerifierKey = "admin_verifier";

// Fixed, non-secret timing-calibration inputs (plan #5: "Calibrate
// iterations to 250-500 ms"). Never used as a real credential or salt.
constexpr const char* kCalibrationPassword = "zgw-pbkdf2-calibration";
constexpr uint8_t kCalibrationSalt[kAdminVerifierSaltBytes] = {
    0x5A, 0x47, 0x57, 0x2D, 0x63, 0x61, 0x6C, 0x69, 0x62, 0x72, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x21,
};

// Bounded retry count for calibrate_pbkdf2_iterations() -- never loops
// unboundedly regardless of how the measured device behaves.
constexpr int kCalibrationMaxAttempts = 6;

#ifdef ESP_PLATFORM

void pbkdf2_hmac_sha256(
    const uint8_t* password, uint32_t password_len,
    const uint8_t* salt, uint32_t salt_len,
    uint32_t iterations,
    uint8_t* out, uint32_t out_len) {
    // mbedtls_pkcs5_pbkdf2_hmac_ext -- the non-deprecated variant
    // (confirmed against the real header inside
    // espressif/idf:release-v5.5 before writing this call site; the
    // deprecated mbedtls_pkcs5_pbkdf2_hmac() takes an
    // mbedtls_md_context_t* instead and is not used here).
    (void)mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256, password, password_len, salt, salt_len, iterations, out_len, out);
}

#else  // !ESP_PLATFORM

// Self-contained, host-only SHA-256 / HMAC-SHA256 / PBKDF2-HMAC-SHA256,
// validated byte-for-byte against Python's hashlib.sha256/hmac/
// pbkdf2_hmac before being trusted (see this sub-slice's evidence
// file). NEVER compiled into a production (ESP_PLATFORM) build -- see
// admin_verifier.hpp's own top-of-file comment for why this exists at
// all (no crypto library dev headers on the host toolchain image).

constexpr uint32_t kSha256K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr uint32_t kSha256BlockBytes = 64U;
constexpr uint32_t kSha256DigestBytes = 32U;

uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

// `len` must be small enough that the padded message fits `kMaxPadded`
// (every call site in this file passes at most kSha256BlockBytes +
// kSha256DigestBytes = 96 bytes, well under the bound below).
constexpr uint32_t kSha256MaxInputBytes = 256U;
constexpr uint32_t kSha256MaxPaddedBytes = kSha256MaxInputBytes + kSha256BlockBytes + 8U;

void host_sha256(const uint8_t* data, uint32_t len, uint8_t out[kSha256DigestBytes]) {
    uint32_t h[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };

    uint8_t msg[kSha256MaxPaddedBytes];
    uint32_t new_len = len + 1U;
    while (new_len % kSha256BlockBytes != 56U) {
        new_len++;
    }
    std::memcpy(msg, data, len);
    msg[len] = 0x80U;
    for (uint32_t i = len + 1U; i < new_len; ++i) {
        msg[i] = 0U;
    }
    const uint64_t bits_len = (uint64_t)len * 8U;
    for (int i = 0; i < 8; ++i) {
        msg[new_len + (uint32_t)i] = (uint8_t)(bits_len >> (56 - 8 * i));
    }
    const uint32_t total_len = new_len + 8U;

    for (uint32_t chunk = 0U; chunk < total_len; chunk += kSha256BlockBytes) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint32_t o = chunk + (uint32_t)i * 4U;
            w[i] = (uint32_t(msg[o]) << 24) | (uint32_t(msg[o + 1]) << 16) | (uint32_t(msg[o + 2]) << 8) |
                   (uint32_t(msg[o + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = hh + s1 + ch + kSha256K[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

// `key_len` <= kSha256BlockBytes and `msg_len` <= kSha256MaxInputBytes -
// kSha256BlockBytes at every call site in this file (this project's
// passwords/salts/digests are all far smaller than that bound).
void host_hmac_sha256(
    const uint8_t* key, uint32_t key_len, const uint8_t* msg, uint32_t msg_len, uint8_t out[kSha256DigestBytes]) {
    uint8_t key_block[kSha256BlockBytes] = {0};
    if (key_len > kSha256BlockBytes) {
        host_sha256(key, key_len, key_block);
    } else {
        std::memcpy(key_block, key, key_len);
    }

    uint8_t ipad[kSha256BlockBytes];
    uint8_t opad[kSha256BlockBytes];
    for (uint32_t i = 0; i < kSha256BlockBytes; ++i) {
        ipad[i] = key_block[i] ^ 0x36U;
        opad[i] = key_block[i] ^ 0x5cU;
    }

    uint8_t inner_buf[kSha256MaxInputBytes];
    std::memcpy(inner_buf, ipad, kSha256BlockBytes);
    std::memcpy(inner_buf + kSha256BlockBytes, msg, msg_len);
    uint8_t inner_hash[kSha256DigestBytes];
    host_sha256(inner_buf, kSha256BlockBytes + msg_len, inner_hash);

    uint8_t outer_buf[kSha256BlockBytes + kSha256DigestBytes];
    std::memcpy(outer_buf, opad, kSha256BlockBytes);
    std::memcpy(outer_buf + kSha256BlockBytes, inner_hash, kSha256DigestBytes);
    host_sha256(outer_buf, kSha256BlockBytes + kSha256DigestBytes, out);
}

// Single-block-output PBKDF2-HMAC-SHA256 (dklen fixed at
// kAdminVerifierHashBytes == kSha256DigestBytes == 32, so this project
// never needs PBKDF2's multi-block-output loop).
void pbkdf2_hmac_sha256(
    const uint8_t* password, uint32_t password_len,
    const uint8_t* salt, uint32_t salt_len,
    uint32_t iterations,
    uint8_t* out, uint32_t out_len) {
    uint8_t salt_block[kAdminVerifierSaltBytes + 4U];
    std::memcpy(salt_block, salt, salt_len);
    salt_block[salt_len] = 0U;
    salt_block[salt_len + 1U] = 0U;
    salt_block[salt_len + 2U] = 0U;
    salt_block[salt_len + 3U] = 1U;  // block index 1 (big-endian) -- the only block this project ever derives

    uint8_t u[kSha256DigestBytes];
    host_hmac_sha256(password, password_len, salt_block, salt_len + 4U, u);

    uint8_t t[kSha256DigestBytes];
    std::memcpy(t, u, kSha256DigestBytes);

    for (uint32_t i = 1; i < iterations; ++i) {
        uint8_t u_next[kSha256DigestBytes];
        host_hmac_sha256(password, password_len, u, kSha256DigestBytes, u_next);
        std::memcpy(u, u_next, kSha256DigestBytes);
        for (uint32_t j = 0; j < kSha256DigestBytes; ++j) {
            t[j] ^= u[j];
        }
    }

    const uint32_t to_copy = out_len < kSha256DigestBytes ? out_len : kSha256DigestBytes;
    std::memcpy(out, t, to_copy);
}

#endif  // ESP_PLATFORM

void derive_hash(
    const char* password, const uint8_t salt[kAdminVerifierSaltBytes], uint32_t iterations,
    uint8_t out_hash[kAdminVerifierHashBytes]) {
    pbkdf2_hmac_sha256(
        reinterpret_cast<const uint8_t*>(password), (uint32_t)std::strlen(password),
        salt, kAdminVerifierSaltBytes, iterations, out_hash, kAdminVerifierHashBytes);
}

}  // namespace

void serialize_admin_verifier_record(
    const AdminVerifierRecord& record, uint8_t out[kAdminVerifierRecordBytes]) noexcept {
    std::memcpy(out, record.salt, kAdminVerifierSaltBytes);
    std::memcpy(out + kAdminVerifierSaltBytes, record.hash, kAdminVerifierHashBytes);
    const uint32_t offset = kAdminVerifierSaltBytes + kAdminVerifierHashBytes;
    out[offset] = (uint8_t)(record.iterations & 0xFFU);
    out[offset + 1] = (uint8_t)((record.iterations >> 8) & 0xFFU);
    out[offset + 2] = (uint8_t)((record.iterations >> 16) & 0xFFU);
    out[offset + 3] = (uint8_t)((record.iterations >> 24) & 0xFFU);
}

bool deserialize_admin_verifier_record(
    const uint8_t* bytes, uint32_t bytes_len, AdminVerifierRecord* out) noexcept {
    if (bytes == nullptr || out == nullptr || bytes_len != kAdminVerifierRecordBytes) {
        return false;
    }
    AdminVerifierRecord record{};
    std::memcpy(record.salt, bytes, kAdminVerifierSaltBytes);
    std::memcpy(record.hash, bytes + kAdminVerifierSaltBytes, kAdminVerifierHashBytes);
    const uint32_t offset = kAdminVerifierSaltBytes + kAdminVerifierHashBytes;
    record.iterations = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                         ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
    *out = record;
    return true;
}

uint32_t measure_pbkdf2_duration_ms(uint32_t iterations) noexcept {
    uint8_t dummy_hash[kAdminVerifierHashBytes];
    const uint64_t start_ms = hal_time_now_ms();
    derive_hash(kCalibrationPassword, kCalibrationSalt, iterations, dummy_hash);
    const uint64_t end_ms = hal_time_now_ms();
    return (uint32_t)(end_ms - start_ms);
}

uint32_t calibrate_pbkdf2_iterations(uint32_t target_min_ms, uint32_t target_max_ms) noexcept {
    uint32_t iterations = kAdminVerifierMinIterations;
    const uint32_t target_mid_ms = (target_min_ms + target_max_ms) / 2U;

    for (int attempt = 0; attempt < kCalibrationMaxAttempts; ++attempt) {
        const uint32_t duration_ms = measure_pbkdf2_duration_ms(iterations);
        if (duration_ms >= target_min_ms && duration_ms <= target_max_ms) {
            break;
        }
        if (duration_ms == 0U) {
            // Too fast to measure meaningfully at this iteration count --
            // double it and remeasure rather than dividing by zero.
            iterations = iterations * 2U;
        } else {
            const uint64_t scaled = (uint64_t)iterations * (uint64_t)target_mid_ms / (uint64_t)duration_ms;
            iterations = (uint32_t)scaled;
        }
        if (iterations < kAdminVerifierMinIterations) {
            iterations = kAdminVerifierMinIterations;
        }
    }

    if (iterations < kAdminVerifierMinIterations) {
        iterations = kAdminVerifierMinIterations;
    }
    return iterations;
}

bool create_admin_verifier(const char* password, AdminVerifierRecord* out) noexcept {
    if (password == nullptr || password[0] == '\0' || out == nullptr) {
        return false;
    }

    AdminVerifierRecord record{};
    if (hal_random_fill_bytes(record.salt, kAdminVerifierSaltBytes) != HAL_RANDOM_STATUS_OK) {
        return false;
    }

    record.iterations = calibrate_pbkdf2_iterations(
        kAdminVerifierCalibrationTargetMinMs, kAdminVerifierCalibrationTargetMaxMs);
    derive_hash(password, record.salt, record.iterations, record.hash);

    *out = record;
    return true;
}

bool verify_admin_password(const char* password, const AdminVerifierRecord& record) noexcept {
    if (password == nullptr || password[0] == '\0') {
        return false;
    }
    // A stored iteration count below the hard minimum indicates a
    // corrupt or tampered record -- never trust it to gate its own
    // verification cost (plan #5's hard minimum is an invariant, not a
    // suggestion).
    if (record.iterations < kAdminVerifierMinIterations) {
        return false;
    }

    uint8_t candidate_hash[kAdminVerifierHashBytes];
    derive_hash(password, record.salt, record.iterations, candidate_hash);

    // Constant-time compare -- never short-circuits on the first
    // mismatched byte (timing-attack resistance).
    uint8_t diff = 0U;
    for (uint32_t i = 0; i < kAdminVerifierHashBytes; ++i) {
        diff = (uint8_t)(diff | (candidate_hash[i] ^ record.hash[i]));
    }
    return diff == 0U;
}

SecureStorageStatus get_stored_admin_verifier(AdminVerifierRecord* out) noexcept {
    if (out == nullptr) {
        return SecureStorageStatus::kUnavailable;
    }

    uint8_t bytes[kAdminVerifierRecordBytes];
    uint32_t bytes_len = 0U;
    const SecureStorageStatus status = secure_storage_get_blob(
        NvsNamespaceId::kAdminVerifier, kAdminVerifierKey, bytes, sizeof(bytes), &bytes_len);

    const bool content_valid =
        status == SecureStorageStatus::kAvailable && deserialize_admin_verifier_record(bytes, bytes_len, out);
    return secure_storage_downgrade_to_corrupt_if_invalid(status, content_valid);
}

SecureStorageWriteResult set_stored_admin_verifier(const AdminVerifierRecord& record) noexcept {
    uint8_t bytes[kAdminVerifierRecordBytes];
    serialize_admin_verifier_record(record, bytes);
    return secure_storage_set_blob(NvsNamespaceId::kAdminVerifier, kAdminVerifierKey, bytes, sizeof(bytes));
}

bool admin_credential_exists() noexcept {
    AdminVerifierRecord record{};
    return get_stored_admin_verifier(&record) == SecureStorageStatus::kAvailable;
}

}  // namespace service
