/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Plan S6 "HTTPS and sessions" #10, #11:
// #10: "Production uses the S5 encrypted `current` certificate/key slot.
//     Its certificate must chain to the configured product management CA
//     and contain both the exact mDNS DNS SAN and URI SAN
//     `urn:zgw:<gateway_id>`. Development profile may generate a visibly
//     development-only self-signed certificate. Production never
//     generates or accepts an untracked self-signed certificate."
// #11: "Product CA trust is distributed to admin/browser clients out of
//     band. Missing CA, invalid issuer/SAN/expiry/key match or unreadable
//     current slot keeps the production management listener disabled."
//
// Lives in app_hal (a plain C `hal_*` module), not `components/service`,
// deliberately: this module calls real mbedtls X.509/PK parsing
// functions that internally heap-allocate and must be explicitly freed
// (mbedtls_x509_crt_free()/mbedtls_pk_free()). `check_arch_invariants.sh`'s
// INV-H002 ("service layer must not use malloc/calloc/realloc/free")
// caught exactly this when the first version of this module was written
// under `components/service` -- a real, correct architectural-gate
// finding, not a false positive: `mbedtls_x509_crt_free`/`mbedtls_pk_free`
// are mbedtls's own RAII-style cleanup for its internally-managed heap
// buffers, exactly the same category of call `hal_ota.c`'s own
// pre-existing `mbedtls_pk_free()`/`mbedtls_ecdsa_free()` calls already
// make -- `components/app_hal` is where this project's own architecture
// already puts that kind of code, and INV-H002's check does not scope to
// `components/app_hal` at all. Relocated here instead of suppressed via
// `docs/architecture/ADR_EXCEPTIONS.md` (that mechanism is for temporary
// exceptions with an expiry date, not a permanent architectural fit).
//
// This module validates an already-read certificate/private-key/CA byte
// triple -- it does not itself read from storage.
// `components/web_ui/web_server.cpp`'s own `start_production_https()`
// (plan #7) already gates on `SecureStorageStatus::kAvailable` for all
// three before calling anything here, satisfying #11's "unreadable
// current slot" clause on its own. This module is the "invalid
// issuer/SAN/expiry/key match" clause specifically.
//
// Real mbedtls API, confirmed against the real headers AND
// implementation inside `espressif/idf:release-v5.5` before writing any
// code: `mbedtls_x509_crt_verify()` performs chain-to-CA validation,
// expiry/not-yet-valid checking (`MBEDTLS_X509_BADCERT_EXPIRED`/
// `_FUTURE`), and a Subject Alternative Name match against its `cn`
// parameter, all in one call -- confirmed by reading the library's own
// `x509_crt_verify_name()`/`x509_crt_check_san()` implementation, not
// assumed from the header comment alone. Since exactly two independent
// SAN values (a DNS name AND a URI) must each be present, and
// `mbedtls_x509_crt_verify()`'s `cn` parameter checks only one identity
// value per call, this module calls it twice against the same parsed
// chain rather than hand-rolling its own SAN-list walk (trusting
// mbedtls's own exact-match logic rather than re-implementing it is the
// safer choice for security-sensitive comparison code). Private-key/
// certificate pairing uses `mbedtls_pk_check_pair()`. Randomness for
// blinding operations comes from ESP-IDF's own `mbedtls_esp_random()`
// (`mbedtls/esp_mbedtls_random.h`, a real, public, documented "suitable
// for passing as f_rng" wrapper around `esp_fill_random()` -- confirmed
// via its own header comment, not assumed).
//
// A known, out-of-scope caveat, not solved by this module: expiry
// checking depends on the device having a roughly correct wall-clock
// time (NTP sync or a battery-backed RTC) -- neither is addressed here.
//
// ESP_PLATFORM-only, with no host equivalent, matching this project's
// established convention for real crypto logic that cannot be
// meaningfully host-tested (the host toolchain has zero crypto library
// dev headers at all, the same blocker `admin_verifier.cpp`'s own
// PBKDF2 work found -- and unlike SHA-256/HMAC/PBKDF2, hand-rolling a
// test-only X.509 parser/verifier would be far too complex and risky to
// trust). Verified only via a real `idf.py build`, the same bar
// `hal_ota.c`'s own un-host-tested manifest-signature verification is
// held to.

typedef enum {
    HAL_TLS_CERT_VALIDATION_VALID = 0,
    HAL_TLS_CERT_VALIDATION_CERTIFICATE_PARSE_FAILED = 1,
    HAL_TLS_CERT_VALIDATION_PRIVATE_KEY_PARSE_FAILED = 2,
    HAL_TLS_CERT_VALIDATION_CA_PARSE_FAILED = 3,
    // Chain-to-CA validation, expiry/not-yet-valid checking, or the DNS
    // SAN match against `expected_dns_san` failed (mbedtls's own
    // combined mbedtls_x509_crt_verify() result).
    HAL_TLS_CERT_VALIDATION_CHAIN_OR_DNS_SAN_INVALID = 4,
    // Chain-to-CA/expiry passed (checked again, redundantly but
    // harmlessly, by the second mbedtls_x509_crt_verify() call), but the
    // URI SAN did not match `expected_uri_san`.
    HAL_TLS_CERT_VALIDATION_URI_SAN_INVALID = 5,
    // The private key does not correspond to the certificate's public
    // key (mbedtls_pk_check_pair()).
    HAL_TLS_CERT_VALIDATION_KEY_MISMATCH = 6,
} hal_tls_cert_validation_result_t;

// Validates `cert_pem`/`key_pem`/`ca_pem` (each NUL-terminated PEM text,
// `_len` including the terminating NUL byte -- mbedtls's own PEM
// convention) against `expected_dns_san` (the full DNS name a client
// would connect to, e.g. "zigbee-gateway-abcdef.local" -- include the
// ".local" suffix; `service::build_gateway_mdns_host()` alone does not)
// and `expected_uri_san` (`service::build_gateway_uri_san()`'s own
// output, e.g. "urn:zgw:abcdef123456"). Returns
// HAL_TLS_CERT_VALIDATION_VALID only if the certificate parses, chains
// to `ca_pem`, is currently within its validity period, contains both
// SAN values exactly, and the private key matches the certificate's
// public key.
hal_tls_cert_validation_result_t hal_tls_validate_certificate(
    const uint8_t* cert_pem, uint32_t cert_pem_len, const uint8_t* key_pem, uint32_t key_pem_len,
    const uint8_t* ca_pem, uint32_t ca_pem_len, const char* expected_dns_san, const char* expected_uri_san);

#ifdef __cplusplus
}
#endif
