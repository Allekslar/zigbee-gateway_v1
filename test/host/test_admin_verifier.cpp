/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "admin_verifier.hpp"
#include "hal_security_state_test.h"

// admin_credential_exists() must be checked before any other test in
// this file writes to the kAdminVerifier namespace -- hal_nvs.c's host
// backend is a single static in-process store with no per-test reset,
// so "not yet provisioned" can only be observed once, first.
namespace {

using service::AdminVerifierRecord;
using service::SecureStorageStatus;
using service::SecureStorageWriteResult;

void test_admin_credential_does_not_exist_before_any_write() {
    assert(!service::admin_credential_exists());
}

void test_get_stored_admin_verifier_is_not_provisioned_before_any_write() {
    AdminVerifierRecord record{};
    assert(service::get_stored_admin_verifier(&record) == SecureStorageStatus::kNotProvisioned);
}

void test_serialize_deserialize_round_trips() {
    AdminVerifierRecord record{};
    for (uint32_t i = 0; i < service::kAdminVerifierSaltBytes; ++i) record.salt[i] = (uint8_t)(i + 1);
    for (uint32_t i = 0; i < service::kAdminVerifierHashBytes; ++i) record.hash[i] = (uint8_t)(i + 100);
    record.iterations = 123456U;

    uint8_t bytes[service::kAdminVerifierRecordBytes];
    service::serialize_admin_verifier_record(record, bytes);

    AdminVerifierRecord round_tripped{};
    assert(service::deserialize_admin_verifier_record(bytes, sizeof(bytes), &round_tripped));
    assert(std::memcmp(round_tripped.salt, record.salt, sizeof(record.salt)) == 0);
    assert(std::memcmp(round_tripped.hash, record.hash, sizeof(record.hash)) == 0);
    assert(round_tripped.iterations == record.iterations);
}

void test_deserialize_rejects_wrong_length() {
    uint8_t bytes[service::kAdminVerifierRecordBytes - 1]{};
    AdminVerifierRecord record{};
    assert(!service::deserialize_admin_verifier_record(bytes, sizeof(bytes), &record));
}

void test_deserialize_rejects_null_args() {
    uint8_t bytes[service::kAdminVerifierRecordBytes]{};
    AdminVerifierRecord record{};
    assert(!service::deserialize_admin_verifier_record(nullptr, sizeof(bytes), &record));
    assert(!service::deserialize_admin_verifier_record(bytes, sizeof(bytes), nullptr));
}

void test_measure_pbkdf2_duration_ms_returns_without_crashing() {
    // Not a timing assertion (host CPU speed is unpredictable and this
    // must never be flaky) -- just confirms the call completes and
    // returns a plausible (not obviously garbage) value.
    const uint32_t duration_ms = service::measure_pbkdf2_duration_ms(service::kAdminVerifierMinIterations);
    assert(duration_ms < 60000U);  // sanity bound: must not take a full minute
}

void test_calibrate_pbkdf2_iterations_never_goes_below_hard_minimum() {
    const uint32_t iterations = service::calibrate_pbkdf2_iterations(
        service::kAdminVerifierCalibrationTargetMinMs, service::kAdminVerifierCalibrationTargetMaxMs);
    assert(iterations >= service::kAdminVerifierMinIterations);
}

void test_create_admin_verifier_rejects_null_or_empty_password() {
    AdminVerifierRecord record{};
    assert(!service::create_admin_verifier(nullptr, &record));
    assert(!service::create_admin_verifier("", &record));
}

void test_create_admin_verifier_rejects_null_out() {
    assert(!service::create_admin_verifier("s0me-password", nullptr));
}

void test_create_admin_verifier_uses_at_least_the_hard_minimum_iterations() {
    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(record.iterations >= service::kAdminVerifierMinIterations);
}

void test_create_and_verify_admin_verifier_round_trip() {
    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(service::verify_admin_password("correct horse battery staple", record));
}

void test_verify_admin_password_rejects_wrong_password() {
    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(!service::verify_admin_password("wrong password entirely", record));
}

void test_verify_admin_password_rejects_null_or_empty_password() {
    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(!service::verify_admin_password(nullptr, record));
    assert(!service::verify_admin_password("", record));
}

void test_verify_admin_password_rejects_tampered_low_iteration_record() {
    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    record.iterations = service::kAdminVerifierMinIterations - 1U;  // tampered below the hard minimum
    assert(!service::verify_admin_password("correct horse battery staple", record));
}

void test_two_verifiers_for_the_same_password_have_different_salts() {
    AdminVerifierRecord first{};
    AdminVerifierRecord second{};
    assert(service::create_admin_verifier("correct horse battery staple", &first));
    assert(service::create_admin_verifier("correct horse battery staple", &second));
    assert(std::memcmp(first.salt, second.salt, sizeof(first.salt)) != 0);
}

void test_set_and_get_stored_admin_verifier_round_trip() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(service::set_stored_admin_verifier(record) == SecureStorageWriteResult::kWritten);

    AdminVerifierRecord read_back{};
    assert(service::get_stored_admin_verifier(&read_back) == SecureStorageStatus::kAvailable);
    assert(std::memcmp(read_back.salt, record.salt, sizeof(record.salt)) == 0);
    assert(std::memcmp(read_back.hash, record.hash, sizeof(record.hash)) == 0);
    assert(read_back.iterations == record.iterations);

    hal_security_state_reset_mock_flash_encryption_enabled();
}

void test_set_stored_admin_verifier_rejected_when_encryption_not_verified() {
    hal_security_state_reset_mock_flash_encryption_enabled();  // false -- "not verified"

    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(service::set_stored_admin_verifier(record) == SecureStorageWriteResult::kRejectedEncryptionNotVerified);
}

void test_admin_credential_exists_true_after_a_verifier_is_stored() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    AdminVerifierRecord record{};
    assert(service::create_admin_verifier("correct horse battery staple", &record));
    assert(service::set_stored_admin_verifier(record) == SecureStorageWriteResult::kWritten);
    assert(service::admin_credential_exists());

    hal_security_state_reset_mock_flash_encryption_enabled();
}

}  // namespace

int main() {
    test_admin_credential_does_not_exist_before_any_write();
    test_get_stored_admin_verifier_is_not_provisioned_before_any_write();
    test_serialize_deserialize_round_trips();
    test_deserialize_rejects_wrong_length();
    test_deserialize_rejects_null_args();
    test_measure_pbkdf2_duration_ms_returns_without_crashing();
    test_calibrate_pbkdf2_iterations_never_goes_below_hard_minimum();
    test_create_admin_verifier_rejects_null_or_empty_password();
    test_create_admin_verifier_rejects_null_out();
    test_create_admin_verifier_uses_at_least_the_hard_minimum_iterations();
    test_create_and_verify_admin_verifier_round_trip();
    test_verify_admin_password_rejects_wrong_password();
    test_verify_admin_password_rejects_null_or_empty_password();
    test_verify_admin_password_rejects_tampered_low_iteration_record();
    test_two_verifiers_for_the_same_password_have_different_salts();
    test_set_stored_admin_verifier_rejected_when_encryption_not_verified();
    test_set_and_get_stored_admin_verifier_round_trip();
    test_admin_credential_exists_true_after_a_verifier_is_stored();
    return 0;
}
