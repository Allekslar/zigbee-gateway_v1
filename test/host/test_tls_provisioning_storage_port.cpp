/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "hal_security_state_test.h"
#include "tls_provisioning_storage_port.hpp"

// Every byte pattern in this file is synthetic placeholder data (0xAA/0xBB/
// 0xCC-style filler), never anything resembling a real private key,
// certificate or provisioning secret -- matching plan #13's own text
// ("this stage does not generate untracked production certificates or
// shared secrets").

namespace {

using service::SecureStorageStatus;
using service::SecureStorageWriteResult;
using service::TlsCertificateSlot;

void test_private_key_round_trips_per_slot_once_encryption_verified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const uint8_t current_key[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    const uint8_t next_key[4] = {0xB1, 0xB2, 0xB3, 0xB4};

    assert(
        service::tls_identity_set_private_key(TlsCertificateSlot::kCurrent, current_key, sizeof(current_key)) ==
        SecureStorageWriteResult::kWritten);
    assert(
        service::tls_identity_set_private_key(TlsCertificateSlot::kNext, next_key, sizeof(next_key)) ==
        SecureStorageWriteResult::kWritten);

    uint8_t current_readback[4]{};
    uint32_t current_len = 0U;
    assert(
        service::tls_identity_get_private_key(
            TlsCertificateSlot::kCurrent, current_readback, sizeof(current_readback), &current_len) ==
        SecureStorageStatus::kAvailable);
    assert(current_len == sizeof(current_key));
    assert(std::memcmp(current_readback, current_key, sizeof(current_key)) == 0);

    uint8_t next_readback[4]{};
    uint32_t next_len = 0U;
    assert(
        service::tls_identity_get_private_key(TlsCertificateSlot::kNext, next_readback, sizeof(next_readback), &next_len) ==
        SecureStorageStatus::kAvailable);
    assert(next_len == sizeof(next_key));
    assert(std::memcmp(next_readback, next_key, sizeof(next_key)) == 0);

    // The two slots are genuinely independent storage -- writing "current"
    // never touched "next" and vice versa.
    assert(std::memcmp(current_readback, next_readback, sizeof(current_key)) != 0);

    hal_security_state_reset_mock_flash_encryption_enabled();
}

// The heart of plan #13's dependency on #12: kTlsIdentity is
// encryption_required in the Section 2.7 registry, so a write must be
// rejected -- and provably not reach storage -- while encryption is
// unverified.
void test_certificate_write_rejected_when_encryption_unverified() {
    hal_security_state_reset_mock_flash_encryption_enabled();

    const uint8_t cert_bytes[3] = {0xC1, 0xC2, 0xC3};
    const SecureStorageWriteResult result =
        service::tls_identity_set_certificate(TlsCertificateSlot::kCurrent, cert_bytes, sizeof(cert_bytes));
    assert(result == SecureStorageWriteResult::kRejectedEncryptionNotVerified);

    uint8_t readback[3]{};
    uint32_t readback_len = 0U;
    assert(
        service::tls_identity_get_certificate(TlsCertificateSlot::kCurrent, readback, sizeof(readback), &readback_len) ==
        SecureStorageStatus::kNotProvisioned);
}

void test_product_ca_round_trips_once_encryption_verified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const uint8_t ca_bytes[5] = {0xD1, 0xD2, 0xD3, 0xD4, 0xD5};
    assert(service::tls_identity_set_product_ca(ca_bytes, sizeof(ca_bytes)) == SecureStorageWriteResult::kWritten);

    uint8_t readback[5]{};
    uint32_t readback_len = 0U;
    assert(
        service::tls_identity_get_product_ca(readback, sizeof(readback), &readback_len) ==
        SecureStorageStatus::kAvailable);
    assert(readback_len == sizeof(ca_bytes));
    assert(std::memcmp(readback, ca_bytes, sizeof(ca_bytes)) == 0);

    hal_security_state_reset_mock_flash_encryption_enabled();
}

// Must run before any test writes "mfg_pop" or "mfg_efuse_rec".
void test_manufacturing_records_not_provisioned_before_any_write() {
    uint8_t pop_readback[2]{};
    uint32_t pop_len = 0U;
    assert(
        service::manufacturing_provisioning_get_proof_of_possession(pop_readback, sizeof(pop_readback), &pop_len) ==
        SecureStorageStatus::kNotProvisioned);

    uint8_t record_readback[6]{};
    uint32_t record_len = 0U;
    assert(
        service::manufacturing_provisioning_get_efuse_record(record_readback, sizeof(record_readback), &record_len) ==
        SecureStorageStatus::kNotProvisioned);
}

void test_manufacturing_pop_and_efuse_record_are_independent_slots() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const uint8_t pop_bytes[2] = {0xE1, 0xE2};
    const uint8_t efuse_record_bytes[6] = {0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6};

    assert(
        service::manufacturing_provisioning_set_proof_of_possession(pop_bytes, sizeof(pop_bytes)) ==
        SecureStorageWriteResult::kWritten);
    assert(
        service::manufacturing_provisioning_set_efuse_record(efuse_record_bytes, sizeof(efuse_record_bytes)) ==
        SecureStorageWriteResult::kWritten);

    uint8_t pop_readback[2]{};
    uint32_t pop_len = 0U;
    assert(
        service::manufacturing_provisioning_get_proof_of_possession(pop_readback, sizeof(pop_readback), &pop_len) ==
        SecureStorageStatus::kAvailable);
    assert(pop_len == sizeof(pop_bytes));
    assert(std::memcmp(pop_readback, pop_bytes, sizeof(pop_bytes)) == 0);

    uint8_t record_readback[6]{};
    uint32_t record_len = 0U;
    assert(
        service::manufacturing_provisioning_get_efuse_record(record_readback, sizeof(record_readback), &record_len) ==
        SecureStorageStatus::kAvailable);
    assert(record_len == sizeof(efuse_record_bytes));
    assert(std::memcmp(record_readback, efuse_record_bytes, sizeof(efuse_record_bytes)) == 0);

    hal_security_state_reset_mock_flash_encryption_enabled();
}

}  // namespace

int main() {
    test_manufacturing_records_not_provisioned_before_any_write();
    test_private_key_round_trips_per_slot_once_encryption_verified();
    test_certificate_write_rejected_when_encryption_unverified();
    test_product_ca_round_trips_once_encryption_verified();
    test_manufacturing_pop_and_efuse_record_are_independent_slots();
    return 0;
}
