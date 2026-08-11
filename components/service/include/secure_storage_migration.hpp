/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "nvs_namespace_registry.hpp"
#include "secure_storage_port.hpp"

namespace service {

// Plan S5 required change #11 (Encrypted storage foundation): "Add
// restart-safe migration scaffolding for legacy plaintext values: read
// legacy, validate, write encrypted, read back/verify, then erase
// plaintext. No automatic migration executes until S6
// authorization/physical-presence policy exists."
//
// This is scaffolding ONLY: secure_storage_migrate_str() below is a fully
// implemented, fully tested function, but nothing in this repository
// calls it -- no `main/app_main.cpp` boot hook, no S6 authorization flow
// (which does not exist yet). Wiring it into a real boot/authorization
// path is future work, matching S5's established per-item sub-slicing
// discipline (see docs/security/PRODUCTION_HARDENING.md Section 2.10).
//
// Built directly on Section 2.7's namespace registry (#9/#15) and Section
// 2.8/2.9's typed read/write/erase port (#10/#12): every step below goes
// through secure_storage_get_str/set_str/erase, so the same namespace-
// ownership and encryption-verified-write guarantees those provide apply
// here automatically, without this file re-implementing them.
//
// Sequence and restart-safety, matching the plan's own wording plus the
// "Error, transaction and concurrency behavior" contract text ("Interrupted
// encrypted migration remains restart-safe and never deletes plaintext
// before encrypted readback succeeds"):
//   1. Check the DESTINATION first. If it already holds a valid value,
//      a prior call already completed the read/validate/write/verify
//      steps -- do not re-read, re-validate or re-write the source; only
//      (re-)attempt the destination cleanup (erasing the source, if
//      distinct from the destination) in case a prior call was
//      interrupted after writing but before erasing. This is what makes
//      the whole sequence restart-safe: the source is only ever erased
//      *after* the destination is confirmed valid, on this call or a
//      previous one, never before.
//   2. Otherwise, read the source (legacy) value. Not-provisioned is not
//      an error (kNoLegacyValue) -- there is nothing to migrate.
//   3. Validate the source value's content (caller-supplied predicate --
//      this module has no opinion on what a valid Wi-Fi SSID/password/
//      MQTT credential/etc. looks like).
//   4. Write the value to the destination via secure_storage_set_str
//      (plan #12's encryption-verified write gate applies automatically;
//      a namespace requiring encryption that isn't verified active yet
//      rejects the write here, and migration simply does not happen this
//      call -- it is safe and correct to retry later).
//   5. Read the destination back and confirm it matches exactly what was
//      just written *before* touching the source at all.
//   6. Only now, erase the source.
//
// Scoping note on source == destination: an earlier draft of this module
// allowed source and destination to name the literal same namespace+key
// (reasoning that this project's approved production profile, Section
// 2.1, uses whole-partition NVS Encryption via Flash Encryption --
// `CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y` -- which is transparent
// below hal_nvs's own API, so a real production migration might simply
// rewrite a value in place through the plan #12 write gate rather than
// move it to a distinct destination). Writing this module's own tests
// surfaced a real correctness problem with that: step 1's restart-safety
// check ("does the destination already hold a valid value?") cannot
// distinguish "a prior call already migrated this" from "the legacy
// plaintext value is simply sitting there, never migrated at all" when
// source and destination are the same slot -- so the encryption-verified
// write gate (#12) would be silently, permanently bypassed for any value
// that already exists. secure_storage_migrate_str() therefore REJECTS a
// same-namespace-and-key call outright (kInvalidSameSourceAndDestination)
// rather than exhibiting that gap. A real migration needs a genuinely
// distinct destination -- either a different key within today's single
// real ESP-IDF namespace, or a real per-category namespace once a future
// sub-slice adds that routing to hal_nvs.c -- before this function can be
// wired to any real credential.
enum class SecureStorageMigrationResult : uint8_t {
    // source_namespace/source_key and dest_namespace/dest_key name the
    // literal same slot -- rejected before touching storage at all. See
    // the scoping note above.
    kInvalidSameSourceAndDestination = 11,
    // Freshly migrated this call: source read, validated, written to the
    // destination, read back and verified, and the source was erased
    // successfully.
    kMigrated = 0,
    // Same as kMigrated, except erasing the now-redundant source failed.
    // The migrated value itself is safe and verified; only the plaintext
    // cleanup did not complete -- safe to retry later (retrying calls
    // secure_storage_erase again without touching the already-migrated
    // destination, since the "destination already valid" restart-safety
    // path handles this).
    kMigratedButLegacyEraseFailed = 1,
    // The destination already held a valid value before this call (a
    // previous call, or some other path, already completed the migration
    // proper). Legacy source cleanup was (re-)attempted as part of this
    // call; see the outcome via a follow-up secure_storage_get_str on the
    // source if the caller needs to know whether cleanup succeeded.
    kAlreadyMigrated = 2,
    // Neither the destination nor the source holds a value -- nothing to
    // migrate. Not an error.
    kNoLegacyValue = 3,
    // secure_storage_get_str(source) returned kUnavailable: wrong
    // namespace/key claim, an unknown key, or a storage-subsystem-level
    // problem (Section 2.8's SecureStorageStatus::kUnavailable itself
    // cannot distinguish which).
    kSourceUnavailable = 4,
    // Same as kSourceUnavailable, but for the destination.
    kDestUnavailable = 5,
    // secure_storage_get_str(source) returned kCorrupt (the stored value
    // does not fit the caller's buffer -- a size/schema mismatch).
    kLegacyValueCorrupt = 6,
    // The caller-supplied validator rejected the source value's content.
    kLegacyValueFailedValidation = 7,
    // Plan #12's write gate rejected the destination write because
    // encryption is not verified active yet. Safe and expected to retry
    // later once it is -- the source value is untouched.
    kWriteRejectedEncryptionNotVerified = 8,
    // The destination write itself failed at the hal_nvs level.
    kWriteFailed = 9,
    // The destination was read back after writing but did not match what
    // was just written -- the source is deliberately left untouched (see
    // the module-level restart-safety note: never erase before verified
    // readback succeeds).
    kReadbackVerificationFailed = 10,
};

// Buffer capacity used internally to hold the value being migrated,
// matching this repository's largest real existing plaintext credential
// buffer (`char password[65]` in network_manager.cpp/
// connectivity_manager.cpp for a 64-character Wi-Fi PSK plus a null
// terminator) -- not an arbitrary number. A value that does not fit is
// treated as kLegacyValueCorrupt (via the same NO_SPACE -> kCorrupt
// mapping Section 2.8's read port already applies).
inline constexpr uint32_t kSecureStorageMigrationMaxValueBytes = 65U;

// A caller-supplied check on the source value's content before it is
// migrated (e.g. "is this a plausible Wi-Fi SSID/password" -- this module
// has no schema-specific knowledge of its own). Return true to accept the
// value for migration.
using SecureStorageMigrationStringValidator = bool (*)(const char* value);

// Performs the restart-safe read/validate/write/verify/erase sequence
// described above for a single string-valued secret. `validator` may be
// nullptr to accept any non-empty source value unconditionally.
SecureStorageMigrationResult secure_storage_migrate_str(
    NvsNamespaceId source_namespace,
    const char* source_key,
    NvsNamespaceId dest_namespace,
    const char* dest_key,
    SecureStorageMigrationStringValidator validator) noexcept;

}  // namespace service
