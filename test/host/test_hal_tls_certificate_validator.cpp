/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "hal_tls_certificate_validator.h"

// Host builds have zero crypto library dev headers available at all
// (confirmed by direct recon, see admin_verifier.cpp's own top-of-file
// comment) -- hal_tls_validate_certificate()'s real mbedtls X.509/PK
// logic is ESP_PLATFORM-only and verified only via a real idf.py build
// (see this sub-slice's evidence file), matching hal_ota.c's own
// pre-existing manifest-signature-verification precedent. This test
// exercises only the host stub's own fail-closed contract: it must never
// report a certificate valid, regardless of input, since it never
// actually inspects any of it.

namespace {

void test_host_stub_always_fails_closed() {
    const uint8_t dummy[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    const hal_tls_cert_validation_result_t result = hal_tls_validate_certificate(
        dummy, sizeof(dummy), dummy, sizeof(dummy), dummy, sizeof(dummy), "zigbee-gateway-abcdef.local",
        "urn:zgw:abcdef123456");
    assert(result != HAL_TLS_CERT_VALIDATION_VALID);
}

void test_host_stub_fails_closed_even_for_null_and_zero_length_input() {
    const hal_tls_cert_validation_result_t result =
        hal_tls_validate_certificate(nullptr, 0U, nullptr, 0U, nullptr, 0U, nullptr, nullptr);
    assert(result != HAL_TLS_CERT_VALIDATION_VALID);
}

}  // namespace

int main() {
    test_host_stub_always_fails_closed();
    test_host_stub_fails_closed_even_for_null_and_zero_length_input();
    return 0;
}
