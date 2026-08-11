/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "secure_storage_migration.hpp"

#include <cstring>

namespace service {

namespace {

bool same_key(NvsNamespaceId namespace_a, const char* key_a, NvsNamespaceId namespace_b, const char* key_b) {
    return namespace_a == namespace_b && std::strcmp(key_a, key_b) == 0;
}

// Erases the source key and folds the result into whichever of
// kMigrated/kAlreadyMigrated the caller already decided on: erase failure
// downgrades kMigrated to kMigratedButLegacyEraseFailed; it does not
// change kAlreadyMigrated, since the migration itself already succeeded on
// a prior call either way and this is best-effort cleanup only. Callers
// only reach this after secure_storage_migrate_str()'s own upfront
// same-key rejection, so source_key/dest_key are always genuinely
// distinct here.
SecureStorageMigrationResult cleanup_source_and_report(
    NvsNamespaceId source_namespace,
    const char* source_key,
    SecureStorageMigrationResult success_result) {
    const SecureStorageEraseResult erase_result = secure_storage_erase(source_namespace, source_key);
    if (erase_result == SecureStorageEraseResult::kErased) {
        return success_result;
    }
    return success_result == SecureStorageMigrationResult::kMigrated
               ? SecureStorageMigrationResult::kMigratedButLegacyEraseFailed
               : success_result;
}

}  // namespace

SecureStorageMigrationResult secure_storage_migrate_str(
    NvsNamespaceId source_namespace,
    const char* source_key,
    NvsNamespaceId dest_namespace,
    const char* dest_key,
    SecureStorageMigrationStringValidator validator) noexcept {
    if (same_key(source_namespace, source_key, dest_namespace, dest_key)) {
        return SecureStorageMigrationResult::kInvalidSameSourceAndDestination;
    }

    char dest_buffer[kSecureStorageMigrationMaxValueBytes]{};
    const SecureStorageStatus dest_status =
        secure_storage_get_str(dest_namespace, dest_key, dest_buffer, sizeof(dest_buffer));

    if (dest_status == SecureStorageStatus::kAvailable) {
        // Restart-safety: a prior call may have written and verified the
        // destination but been interrupted before erasing the source.
        // Finish that cleanup now; do not re-read, re-validate or
        // re-write anything.
        return cleanup_source_and_report(source_namespace, source_key, SecureStorageMigrationResult::kAlreadyMigrated);
    }
    if (dest_status == SecureStorageStatus::kUnavailable) {
        return SecureStorageMigrationResult::kDestUnavailable;
    }
    // dest_status is kNotProvisioned or kCorrupt here -- either way, fall
    // through and attempt a fresh migration, overwriting any corrupt
    // destination bytes with a freshly-validated source value.

    char source_buffer[kSecureStorageMigrationMaxValueBytes]{};
    const SecureStorageStatus source_status =
        secure_storage_get_str(source_namespace, source_key, source_buffer, sizeof(source_buffer));

    if (source_status == SecureStorageStatus::kNotProvisioned) {
        return SecureStorageMigrationResult::kNoLegacyValue;
    }
    if (source_status == SecureStorageStatus::kUnavailable) {
        return SecureStorageMigrationResult::kSourceUnavailable;
    }
    if (source_status == SecureStorageStatus::kCorrupt) {
        return SecureStorageMigrationResult::kLegacyValueCorrupt;
    }

    if (validator != nullptr && !validator(source_buffer)) {
        return SecureStorageMigrationResult::kLegacyValueFailedValidation;
    }

    const SecureStorageWriteResult write_result = secure_storage_set_str(dest_namespace, dest_key, source_buffer);
    if (write_result == SecureStorageWriteResult::kRejectedEncryptionNotVerified) {
        return SecureStorageMigrationResult::kWriteRejectedEncryptionNotVerified;
    }
    if (write_result != SecureStorageWriteResult::kWritten) {
        // kRejectedWrongNamespace is unreachable in practice here (the
        // destination read above already proved dest_key belongs to
        // dest_namespace, via the exact same ownership gate), but is
        // handled defensively rather than assumed impossible.
        return SecureStorageMigrationResult::kWriteFailed;
    }

    // Read back and verify *before* ever touching the source -- the
    // plan's own "read back/verify, THEN erase plaintext" ordering, and
    // this module's own restart-safety contract.
    char readback_buffer[kSecureStorageMigrationMaxValueBytes]{};
    const SecureStorageStatus readback_status =
        secure_storage_get_str(dest_namespace, dest_key, readback_buffer, sizeof(readback_buffer));
    if (readback_status != SecureStorageStatus::kAvailable ||
        std::strcmp(readback_buffer, source_buffer) != 0) {
        return SecureStorageMigrationResult::kReadbackVerificationFailed;
    }

    return cleanup_source_and_report(source_namespace, source_key, SecureStorageMigrationResult::kMigrated);
}

}  // namespace service
