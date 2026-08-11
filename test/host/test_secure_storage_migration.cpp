/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "hal_nvs.h"
#include "hal_security_state_test.h"
#include "secure_storage_migration.hpp"

namespace {

using service::NvsNamespaceId;
using service::SecureStorageMigrationResult;

bool reject_all(const char* value) {
    (void)value;
    return false;
}

void assert_key_absent(const char* key) {
    char buffer[service::kSecureStorageMigrationMaxValueBytes]{};
    assert(hal_nvs_get_str(key, buffer, sizeof(buffer)) == HAL_NVS_STATUS_NOT_FOUND);
}

void assert_key_equals(const char* key, const char* expected) {
    char buffer[service::kSecureStorageMigrationMaxValueBytes]{};
    assert(hal_nvs_get_str(key, buffer, sizeof(buffer)) == HAL_NVS_STATUS_OK);
    assert(std::strcmp(buffer, expected) == 0);
}

// A distinct source and destination is required (see
// secure_storage_migration.hpp's scoping note); this test uses two real
// registered keys from kOperationJournalDiagnostics purely as generic
// string slots to exercise the migration mechanism itself, matching how
// other host tests in this repository reuse real registered keys (e.g.
// "mtep_a"/"mtep_b") as generic test slots unrelated to their production
// meaning.
void test_no_legacy_value_when_both_source_and_dest_empty() {
    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kOperationJournalDiagnostics,
        "ota_dbg_req",
        NvsNamespaceId::kOperationJournalDiagnostics,
        "ota_dbg_step",
        nullptr);
    assert(result == SecureStorageMigrationResult::kNoLegacyValue);
}

void test_migrated_moves_value_and_erases_source() {
    assert(hal_nvs_set_str("ota_dbg_req", "breadcrumb-legacy") == HAL_NVS_STATUS_OK);

    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kOperationJournalDiagnostics,
        "ota_dbg_req",
        NvsNamespaceId::kOperationJournalDiagnostics,
        "ota_dbg_step",
        nullptr);
    assert(result == SecureStorageMigrationResult::kMigrated);

    assert_key_equals("ota_dbg_step", "breadcrumb-legacy");
    assert_key_absent("ota_dbg_req");
}

// Restart-safety: the destination already holds a valid value (simulating
// a prior call that wrote+verified it but was interrupted before erasing
// the source) -- migration must not re-read/re-validate/re-write, only
// finish the source cleanup.
void test_already_migrated_only_cleans_up_source() {
    assert(hal_nvs_set_str("dstate_a", "legacy-still-present") == HAL_NVS_STATUS_OK);
    assert(hal_nvs_set_str("dstate_b", "already-migrated-value") == HAL_NVS_STATUS_OK);

    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kCoreDeviceState, "dstate_a", NvsNamespaceId::kCoreDeviceState, "dstate_b", nullptr);
    assert(result == SecureStorageMigrationResult::kAlreadyMigrated);

    assert_key_absent("dstate_a");
    // The destination is untouched by the restart-safety path -- still
    // exactly what it was before this call, not overwritten from source.
    assert_key_equals("dstate_b", "already-migrated-value");
}

void test_validator_rejects_invalid_legacy_value_without_writing_or_erasing() {
    assert(hal_nvs_set_str("cfg_cmd_tmo_ms", "not-a-plausible-value") == HAL_NVS_STATUS_OK);

    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kZigbeeNetworkDeviceReporting,
        "cfg_cmd_tmo_ms",
        NvsNamespaceId::kZigbeeNetworkDeviceReporting,
        "cfg_cmd_retry",
        reject_all);
    assert(result == SecureStorageMigrationResult::kLegacyValueFailedValidation);

    assert_key_absent("cfg_cmd_retry");
    assert_key_equals("cfg_cmd_tmo_ms", "not-a-plausible-value");
}

// The heart of #11 depending on #12: a secret-namespace migration must be
// rejected -- and the source left completely untouched -- while
// encryption is not verified active.
void test_write_rejected_and_source_untouched_when_encryption_unverified() {
    hal_security_state_reset_mock_flash_encryption_enabled();
    assert(hal_nvs_set_str("wifi_ssid", "MyNetwork") == HAL_NVS_STATUS_OK);

    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kWifiCredentials, "wifi_ssid", NvsNamespaceId::kWifiCredentials, "wifi_password", nullptr);
    assert(result == SecureStorageMigrationResult::kWriteRejectedEncryptionNotVerified);

    assert_key_absent("wifi_password");
    assert_key_equals("wifi_ssid", "MyNetwork");
}

// Continues directly from the previous test's state (source still holds
// "MyNetwork", destination still empty) -- once encryption is verified,
// the same call now succeeds and completes the full sequence.
void test_migrated_once_encryption_becomes_verified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kWifiCredentials, "wifi_ssid", NvsNamespaceId::kWifiCredentials, "wifi_password", nullptr);
    assert(result == SecureStorageMigrationResult::kMigrated);

    assert_key_equals("wifi_password", "MyNetwork");
    assert_key_absent("wifi_ssid");

    hal_security_state_reset_mock_flash_encryption_enabled();
}

void test_same_source_and_destination_rejected_outright() {
    const SecureStorageMigrationResult result = service::secure_storage_migrate_str(
        NvsNamespaceId::kMatterEndpointState, "mtep_a", NvsNamespaceId::kMatterEndpointState, "mtep_a", nullptr);
    assert(result == SecureStorageMigrationResult::kInvalidSameSourceAndDestination);
}

}  // namespace

int main() {
    test_no_legacy_value_when_both_source_and_dest_empty();
    test_migrated_moves_value_and_erases_source();
    test_already_migrated_only_cleans_up_source();
    test_validator_rejects_invalid_legacy_value_without_writing_or_erasing();
    test_write_rejected_and_source_untouched_when_encryption_unverified();
    test_migrated_once_encryption_becomes_verified();
    test_same_source_and_destination_rejected_outright();
    return 0;
}
