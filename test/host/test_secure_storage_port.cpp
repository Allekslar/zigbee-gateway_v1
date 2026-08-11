/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "hal_nvs.h"
#include "hal_security_state_test.h"
#include "secure_storage_port.hpp"

namespace {

using service::NvsNamespaceId;
using service::SecureStorageEraseResult;
using service::SecureStorageStatus;
using service::SecureStorageWriteResult;

void test_classify_raw_status_mapping() {
    assert(service::secure_storage_classify_raw_status(HAL_NVS_STATUS_OK) == SecureStorageStatus::kAvailable);
    assert(
        service::secure_storage_classify_raw_status(HAL_NVS_STATUS_NOT_FOUND) ==
        SecureStorageStatus::kNotProvisioned);
    assert(
        service::secure_storage_classify_raw_status(HAL_NVS_STATUS_NO_SPACE) == SecureStorageStatus::kCorrupt);
    assert(
        service::secure_storage_classify_raw_status(HAL_NVS_STATUS_ERR) == SecureStorageStatus::kUnavailable);
    assert(
        service::secure_storage_classify_raw_status(HAL_NVS_STATUS_INVALID_ARG) ==
        SecureStorageStatus::kUnavailable);
}

void test_downgrade_to_corrupt_if_invalid() {
    assert(
        service::secure_storage_downgrade_to_corrupt_if_invalid(SecureStorageStatus::kAvailable, false) ==
        SecureStorageStatus::kCorrupt);
    assert(
        service::secure_storage_downgrade_to_corrupt_if_invalid(SecureStorageStatus::kAvailable, true) ==
        SecureStorageStatus::kAvailable);
    // A non-available status is never touched by content_valid -- there
    // was nothing to have validated.
    assert(
        service::secure_storage_downgrade_to_corrupt_if_invalid(SecureStorageStatus::kNotProvisioned, false) ==
        SecureStorageStatus::kNotProvisioned);
    assert(
        service::secure_storage_downgrade_to_corrupt_if_invalid(SecureStorageStatus::kUnavailable, true) ==
        SecureStorageStatus::kUnavailable);
    assert(
        service::secure_storage_downgrade_to_corrupt_if_invalid(SecureStorageStatus::kCorrupt, true) ==
        SecureStorageStatus::kCorrupt);
}

void test_fail_closed_pass_is_true_only_for_available() {
    assert(service::secure_storage_fail_closed_pass(SecureStorageStatus::kAvailable));
    assert(!service::secure_storage_fail_closed_pass(SecureStorageStatus::kNotProvisioned));
    assert(!service::secure_storage_fail_closed_pass(SecureStorageStatus::kCorrupt));
    assert(!service::secure_storage_fail_closed_pass(SecureStorageStatus::kUnavailable));
}

// Must run before any test that writes "ota_dbg_req" -- the host hal_nvs
// mock only clears its backing storage on the very first hal_nvs_init()
// call in this process (see test_matter_endpoint_registry.cpp's own note
// on the same behavior); every test function in this binary shares that
// one storage instance.
void test_get_u32_not_provisioned_before_any_write() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    uint32_t value = 0xDEADBEEFU;
    const SecureStorageStatus status =
        service::secure_storage_get_u32(NvsNamespaceId::kOperationJournalDiagnostics, "ota_dbg_req", &value);
    assert(status == SecureStorageStatus::kNotProvisioned);
}

void test_get_u32_available_after_write() {
    assert(hal_nvs_set_u32("ota_dbg_req", 42U) == HAL_NVS_STATUS_OK);
    uint32_t value = 0U;
    const SecureStorageStatus status =
        service::secure_storage_get_u32(NvsNamespaceId::kOperationJournalDiagnostics, "ota_dbg_req", &value);
    assert(status == SecureStorageStatus::kAvailable);
    assert(value == 42U);
}

// "ota_dbg_req" belongs to kOperationJournalDiagnostics, not
// kWifiCredentials -- the ownership mismatch must be caught before
// hal_nvs is ever touched, regardless of whether the key was written.
void test_get_u32_wrong_namespace_returns_unavailable_without_touching_value() {
    uint32_t value = 0xCAFEBABEU;
    const SecureStorageStatus status =
        service::secure_storage_get_u32(NvsNamespaceId::kWifiCredentials, "ota_dbg_req", &value);
    assert(status == SecureStorageStatus::kUnavailable);
    assert(value == 0xCAFEBABEU);
}

void test_get_str_unknown_key_returns_unavailable_without_touching_buffer() {
    char buffer[16];
    std::memset(buffer, 0x5A, sizeof(buffer));
    const SecureStorageStatus status = service::secure_storage_get_str(
        NvsNamespaceId::kWifiCredentials, "totally_unknown_key_xyz", buffer, sizeof(buffer));
    assert(status == SecureStorageStatus::kUnavailable);
    for (char c : buffer) {
        assert(c == 0x5A);
    }
}

void test_get_str_null_key_returns_unavailable() {
    char buffer[16]{};
    const SecureStorageStatus status =
        service::secure_storage_get_str(NvsNamespaceId::kWifiCredentials, nullptr, buffer, sizeof(buffer));
    assert(status == SecureStorageStatus::kUnavailable);
}

void test_get_str_available_after_write() {
    assert(hal_nvs_set_str("wifi_ssid", "TestNetwork") == HAL_NVS_STATUS_OK);
    char buffer[32]{};
    const SecureStorageStatus status =
        service::secure_storage_get_str(NvsNamespaceId::kWifiCredentials, "wifi_ssid", buffer, sizeof(buffer));
    assert(status == SecureStorageStatus::kAvailable);
    assert(std::strcmp(buffer, "TestNetwork") == 0);
}

void test_get_blob_available_after_write() {
    const uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    assert(hal_nvs_set_blob("mtep_b", payload, sizeof(payload)) == HAL_NVS_STATUS_OK);

    uint8_t readback[4]{};
    uint32_t readback_len = 0U;
    const SecureStorageStatus status = service::secure_storage_get_blob(
        NvsNamespaceId::kMatterEndpointState, "mtep_b", readback, sizeof(readback), &readback_len);
    assert(status == SecureStorageStatus::kAvailable);
    assert(readback_len == sizeof(payload));
    assert(std::memcmp(readback, payload, sizeof(payload)) == 0);
}

// A stored value that no longer fits the caller's declared (fixed-schema)
// buffer is exactly the plan #10 "corrupt" signal this port maps
// HAL_NVS_STATUS_NO_SPACE to.
void test_get_blob_corrupt_when_stored_value_too_large_for_buffer() {
    const uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    assert(hal_nvs_set_blob("mtep_a", payload, sizeof(payload)) == HAL_NVS_STATUS_OK);

    uint8_t too_small[1]{};
    uint32_t readback_len = 0U;
    const SecureStorageStatus status = service::secure_storage_get_blob(
        NvsNamespaceId::kMatterEndpointState, "mtep_a", too_small, sizeof(too_small), &readback_len);
    assert(status == SecureStorageStatus::kCorrupt);
}

// A schema-v4 reporting-profile key resolves through the registry's prefix
// pattern ("rptp_") even though it was never explicitly written -- proves
// the ownership gate (and therefore this port) works for dynamically-built
// per-profile keys, not just exact literal keys.
void test_get_u32_dynamic_prefix_key_not_provisioned() {
    uint32_t value = 0U;
    const SecureStorageStatus status =
        service::secure_storage_get_u32(NvsNamespaceId::kZigbeeNetworkDeviceReporting, "rptp_c07", &value);
    assert(status == SecureStorageStatus::kNotProvisioned);
}

// Plan #12: "Ensure no new production secret is written before NVS
// Encryption and required key protection are verified at runtime."
// kOperationJournalDiagnostics has encryption_required == false in the
// Section 2.7 registry, so the write gate must never reject it regardless
// of hal_security_state_flash_encryption_enabled()'s value.
void test_write_precondition_met_true_for_non_secret_namespace_regardless_of_encryption_state() {
    hal_security_state_reset_mock_flash_encryption_enabled();
    assert(service::secure_storage_write_precondition_met(NvsNamespaceId::kOperationJournalDiagnostics));

    hal_security_state_set_mock_flash_encryption_enabled(true);
    assert(service::secure_storage_write_precondition_met(NvsNamespaceId::kOperationJournalDiagnostics));

    hal_security_state_reset_mock_flash_encryption_enabled();
}

// kWifiCredentials has encryption_required == true -- the write gate must
// track hal_security_state_flash_encryption_enabled() exactly.
void test_write_precondition_met_tracks_encryption_state_for_secret_namespace() {
    hal_security_state_reset_mock_flash_encryption_enabled();
    assert(!service::secure_storage_write_precondition_met(NvsNamespaceId::kWifiCredentials));

    hal_security_state_set_mock_flash_encryption_enabled(true);
    assert(service::secure_storage_write_precondition_met(NvsNamespaceId::kWifiCredentials));

    hal_security_state_reset_mock_flash_encryption_enabled();
    assert(!service::secure_storage_write_precondition_met(NvsNamespaceId::kWifiCredentials));
}

void test_set_u32_non_secret_namespace_writes_regardless_of_encryption_state() {
    hal_security_state_reset_mock_flash_encryption_enabled();
    const SecureStorageWriteResult result =
        service::secure_storage_set_u32(NvsNamespaceId::kOperationJournalDiagnostics, "core_rev", 7U);
    assert(result == SecureStorageWriteResult::kWritten);

    uint32_t readback = 0U;
    assert(hal_nvs_get_u32("core_rev", &readback) == HAL_NVS_STATUS_OK);
    assert(readback == 7U);
}

// The heart of plan #12: a secret-namespace write must be rejected --
// and, critically, must never actually reach hal_nvs -- while encryption
// is not verified active.
void test_set_str_secret_namespace_rejected_and_not_written_when_encryption_unverified() {
    hal_security_state_reset_mock_flash_encryption_enabled();

    const SecureStorageWriteResult result =
        service::secure_storage_set_str(NvsNamespaceId::kWifiCredentials, "wifi_password", "hunter2");
    assert(result == SecureStorageWriteResult::kRejectedEncryptionNotVerified);

    char buffer[32]{};
    assert(hal_nvs_get_str("wifi_password", buffer, sizeof(buffer)) == HAL_NVS_STATUS_NOT_FOUND);
}

void test_set_str_secret_namespace_written_when_encryption_verified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const SecureStorageWriteResult result =
        service::secure_storage_set_str(NvsNamespaceId::kWifiCredentials, "wifi_password", "hunter2");
    assert(result == SecureStorageWriteResult::kWritten);

    char buffer[32]{};
    assert(hal_nvs_get_str("wifi_password", buffer, sizeof(buffer)) == HAL_NVS_STATUS_OK);
    assert(std::strcmp(buffer, "hunter2") == 0);

    hal_security_state_reset_mock_flash_encryption_enabled();
}

// Same namespace-ownership gate the read path uses -- verified here for
// the write path specifically, including that a rejected write never
// touches the underlying stored bytes.
void test_set_blob_wrong_namespace_rejected_without_writing() {
    uint8_t before[4]{};
    uint32_t before_len = 0U;
    const hal_nvs_status_t before_status = hal_nvs_get_blob("mtep_a", before, sizeof(before), &before_len);

    const uint8_t attempted_payload[4] = {0x99, 0x99, 0x99, 0x99};
    const SecureStorageWriteResult result = service::secure_storage_set_blob(
        NvsNamespaceId::kWifiCredentials, "mtep_a", attempted_payload, sizeof(attempted_payload));
    assert(result == SecureStorageWriteResult::kRejectedWrongNamespace);

    uint8_t after[4]{};
    uint32_t after_len = 0U;
    const hal_nvs_status_t after_status = hal_nvs_get_blob("mtep_a", after, sizeof(after), &after_len);
    assert(after_status == before_status);
    assert(after_len == before_len);
    if (before_status == HAL_NVS_STATUS_OK) {
        assert(std::memcmp(after, before, sizeof(before)) == 0);
    }
}

// Plan #11's "erase plaintext" step depends on this: an existing key must
// actually disappear (subsequent read reports kNotProvisioned).
void test_erase_existing_key_removes_it() {
    assert(hal_nvs_set_str("ota_dbg_step", "breadcrumb-before-erase") == HAL_NVS_STATUS_OK);

    const SecureStorageEraseResult result =
        service::secure_storage_erase(NvsNamespaceId::kOperationJournalDiagnostics, "ota_dbg_step");
    assert(result == SecureStorageEraseResult::kErased);

    char buffer[32]{};
    assert(hal_nvs_get_str("ota_dbg_step", buffer, sizeof(buffer)) == HAL_NVS_STATUS_NOT_FOUND);
}

// Erase must be idempotent -- a caller retrying a previously-interrupted
// cleanup step should not have to distinguish "already gone" from "just
// removed". Runs after test_erase_existing_key_removes_it, so "ota_dbg_step"
// is already absent here.
void test_erase_nonexistent_key_is_idempotent() {
    const SecureStorageEraseResult result =
        service::secure_storage_erase(NvsNamespaceId::kOperationJournalDiagnostics, "ota_dbg_step");
    assert(result == SecureStorageEraseResult::kErased);
}

void test_erase_wrong_namespace_does_not_erase() {
    uint8_t before[4]{};
    uint32_t before_len = 0U;
    assert(hal_nvs_get_blob("mtep_b", before, sizeof(before), &before_len) == HAL_NVS_STATUS_OK);

    const SecureStorageEraseResult result =
        service::secure_storage_erase(NvsNamespaceId::kWifiCredentials, "mtep_b");
    assert(result == SecureStorageEraseResult::kRejectedWrongNamespace);

    uint8_t after[4]{};
    uint32_t after_len = 0U;
    assert(hal_nvs_get_blob("mtep_b", after, sizeof(after), &after_len) == HAL_NVS_STATUS_OK);
    assert(after_len == before_len);
    assert(std::memcmp(after, before, sizeof(before)) == 0);
}

}  // namespace

int main() {
    test_classify_raw_status_mapping();
    test_downgrade_to_corrupt_if_invalid();
    test_fail_closed_pass_is_true_only_for_available();
    test_get_u32_not_provisioned_before_any_write();
    test_get_u32_available_after_write();
    test_get_u32_wrong_namespace_returns_unavailable_without_touching_value();
    test_get_str_unknown_key_returns_unavailable_without_touching_buffer();
    test_get_str_null_key_returns_unavailable();
    test_get_str_available_after_write();
    test_get_blob_available_after_write();
    test_get_blob_corrupt_when_stored_value_too_large_for_buffer();
    test_get_u32_dynamic_prefix_key_not_provisioned();
    test_write_precondition_met_true_for_non_secret_namespace_regardless_of_encryption_state();
    test_write_precondition_met_tracks_encryption_state_for_secret_namespace();
    test_set_u32_non_secret_namespace_writes_regardless_of_encryption_state();
    test_set_str_secret_namespace_rejected_and_not_written_when_encryption_unverified();
    test_set_str_secret_namespace_written_when_encryption_verified();
    test_set_blob_wrong_namespace_rejected_without_writing();
    test_erase_existing_key_removes_it();
    test_erase_nonexistent_key_is_idempotent();
    test_erase_wrong_namespace_does_not_erase();
    return 0;
}
