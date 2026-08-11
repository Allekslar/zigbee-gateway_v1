/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "hal_nvs.h"
#include "hal_security_state.h"
#include "nvs_namespace_registry.hpp"

namespace service {

// Plan S5 required changes #10, #12 and #14 (Encrypted storage
// foundation).
// #10: "Add storage ports that return typed available, not_provisioned,
// corrupt and unavailable results; production callers must fail closed on
// any non-available state." The "Contracts and invariants" section names
// the unavailable case directly: "Secure-storage failure returns
// secure_storage_unavailable and maps to HTTP 503 when exposed later."
// #12: "Ensure no new production secret is written before NVS Encryption
// and required key protection are verified at runtime."
// #14 (its "logs" clause -- see docs/security/PRODUCTION_HARDENING.md
// Section 2.12 for the other two clauses, crash reports and completion
// evidence): "Redact all key/credential material from logs..." -- every
// diagnostic log statement in secure_storage_port.cpp goes through
// secure_log_redaction.hpp, which never reads a value's content, only its
// length, so a raw secret cannot appear in a log line by construction.
//
// Builds directly on the namespace ownership registry from #9/#15
// (nvs_namespace_registry.hpp, docs/security/PRODUCTION_HARDENING.md
// Section 2.7): every read/write below is first gated by "does this key
// actually belong to the namespace the caller claims?" before touching
// hal_nvs at all -- a real correctness guard the registry makes possible,
// and itself an instance of plan #10's "fail closed" mandate (a caller
// passing the wrong namespace for a key is a programming error, not a
// normal missing-value case). The write gate additionally consults each
// namespace's `encryption_required` flag (also from the Section 2.7
// registry) plus `hal_security_state_flash_encryption_enabled()`
// (`hal_security_state.h`) -- the real, eFuse-backed runtime signal, not
// the Kconfig profile that happened to be compiled in -- before allowing a
// write to any of plan #9's six secret namespaces.
//
// This header/source pair implements ONLY #10's typed-result read path and
// #12's write gate. It deliberately does not implement #11 (restart-safe
// migration scaffolding -- no existing hal_nvs.c call site has been
// changed to use this port; every value still lives in the single
// hardcoded "zigbee_gateway" namespace exactly as before this sub-slice),
// #13 (TLS/provisioning storage interfaces) or #14 (redaction). Those are
// later sub-slices layered on top of this one, matching S5's established
// per-item sub-slicing discipline (see docs/security/
// PRODUCTION_HARDENING.md Section 2.9 for this sub-slice's own status).

enum class SecureStorageStatus : uint8_t {
    kAvailable = 0,
    kNotProvisioned = 1,
    kCorrupt = 2,
    kUnavailable = 3,
};

// Plan #10's own mandate: "production callers must fail closed on any
// non-available state." True only for kAvailable -- every production call
// site consuming a secure_storage_get_* result should gate on this rather
// than hand-rolling per-status checks that could drift from the contract.
constexpr bool secure_storage_fail_closed_pass(SecureStorageStatus status) noexcept {
    return status == SecureStorageStatus::kAvailable;
}

// Maps a raw hal_nvs_status_t (as returned by a hal_nvs_get_* call) to the
// plan #10 four-state classification:
//   HAL_NVS_STATUS_OK          -> kAvailable (tentatively -- see
//                                 secure_storage_downgrade_to_corrupt_if_invalid
//                                 below for callers with their own schema)
//   HAL_NVS_STATUS_NOT_FOUND   -> kNotProvisioned (the key was never
//                                 written -- a normal, expected state
//                                 before provisioning, not a failure)
//   HAL_NVS_STATUS_NO_SPACE    -> kCorrupt (the stored value exists but
//                                 does not fit the caller's declared
//                                 buffer -- a strong signal the stored
//                                 size does not match the expected fixed
//                                 schema)
//   anything else (HAL_NVS_STATUS_ERR, HAL_NVS_STATUS_INVALID_ARG)
//                              -> kUnavailable (a storage-subsystem-level
//                                 problem, not a content problem -- fail
//                                 closed)
SecureStorageStatus secure_storage_classify_raw_status(hal_nvs_status_t raw_status) noexcept;

// Downgrades a tentative kAvailable to kCorrupt if the caller's own
// content check (magic number, schema version, checksum -- schema-specific
// per consumer, not something this generic port can know) rejected the
// bytes it just read. Any non-kAvailable status is returned unchanged --
// there was no content to have validated if the read itself did not
// succeed.
SecureStorageStatus secure_storage_downgrade_to_corrupt_if_invalid(
    SecureStorageStatus status, bool content_valid) noexcept;

// Namespace-ownership-gated typed reads: first confirms `key` actually
// belongs to `namespace_id` per the Section 2.7 registry (returning
// kUnavailable immediately, without calling hal_nvs at all, if it does
// not -- including for a null/empty/entirely-unknown key), then calls the
// matching hal_nvs_get_* and classifies the raw result via
// secure_storage_classify_raw_status(). These do not perform any content/
// schema validation themselves -- callers with a fixed-layout secret
// record should combine the result with
// secure_storage_downgrade_to_corrupt_if_invalid() after inspecting the
// bytes they just read.
//
// These wrap hal_nvs_get_u32/get_str/get_blob directly and so still read
// from hal_nvs.c's single hardcoded "zigbee_gateway" ESP-IDF namespace
// today -- routing to a real distinct per-category namespace is #11's
// migration job, not this port's (see nvs_namespace_registry.hpp).
SecureStorageStatus secure_storage_get_u32(
    NvsNamespaceId namespace_id, const char* key, uint32_t* value_out) noexcept;
SecureStorageStatus secure_storage_get_str(
    NvsNamespaceId namespace_id, const char* key, char* value_out, uint32_t value_out_capacity) noexcept;
SecureStorageStatus secure_storage_get_blob(
    NvsNamespaceId namespace_id,
    const char* key,
    void* value_out,
    uint32_t value_out_capacity,
    uint32_t* value_len_out) noexcept;

enum class SecureStorageWriteResult : uint8_t {
    kWritten = 0,
    // The key does not belong to the claimed namespace per the Section 2.7
    // registry (same guard as the read path's kUnavailable case) -- a
    // programming error, not a runtime condition.
    kRejectedWrongNamespace = 1,
    // Plan #12's own rejection: `namespace_id` is one of plan #9's six
    // encryption_required namespaces, but
    // hal_security_state_flash_encryption_enabled() reports encryption is
    // not verified active right now. Namespaces with
    // encryption_required == false (see Section 2.7) are never rejected
    // for this reason.
    kRejectedEncryptionNotVerified = 2,
    // Ownership and the encryption gate both passed, but the underlying
    // hal_nvs_set_* call itself failed (e.g. HAL_NVS_STATUS_NO_SPACE,
    // HAL_NVS_STATUS_ERR).
    kWriteFailed = 3,
};

// Plan #12's own predicate, exposed separately from the concrete
// hal_nvs_set_* wrappers below so it can be tested directly and reused by
// a future caller that wants to check the gate before doing its own,
// non-hal_nvs write path (e.g. a future TLS/provisioning storage
// interface, plan #13). Always true for a namespace with
// encryption_required == false; for encryption_required == true,
// delegates to hal_security_state_flash_encryption_enabled().
bool secure_storage_write_precondition_met(NvsNamespaceId namespace_id) noexcept;

// Namespace-ownership-gated AND plan-#12-encryption-gated typed writes:
// first confirms `key` belongs to `namespace_id` (kRejectedWrongNamespace
// if not), then checks secure_storage_write_precondition_met()
// (kRejectedEncryptionNotVerified if not), and only then calls the
// matching hal_nvs_set_*. No plaintext write to a plan #9 secret namespace
// can reach hal_nvs through this port while encryption is unverified --
// the fail-closed behavior plan #12 requires. These still write through
// hal_nvs.c's single hardcoded "zigbee_gateway" ESP-IDF namespace today,
// same caveat as the read path above (#11's migration job, not this
// port's).
SecureStorageWriteResult secure_storage_set_u32(
    NvsNamespaceId namespace_id, const char* key, uint32_t value) noexcept;
SecureStorageWriteResult secure_storage_set_str(
    NvsNamespaceId namespace_id, const char* key, const char* value) noexcept;
SecureStorageWriteResult secure_storage_set_blob(
    NvsNamespaceId namespace_id, const char* key, const void* value, uint32_t value_len) noexcept;

enum class SecureStorageEraseResult : uint8_t {
    // The key existed and was erased, OR it did not exist to begin with --
    // erase is deliberately idempotent (a caller retrying a previously-
    // interrupted cleanup step, e.g. plan #11's migration scaffolding's
    // "erase plaintext" step, should not have to distinguish "already
    // gone" from "just removed").
    kErased = 0,
    kRejectedWrongNamespace = 1,
    kEraseFailed = 2,
};

// Namespace-ownership-gated typed erase, following the same guard as the
// read/write paths above. Wraps hal_nvs_erase_key(); still erases from
// hal_nvs.c's single hardcoded "zigbee_gateway" ESP-IDF namespace today
// (see the read/write path comments above for why).
SecureStorageEraseResult secure_storage_erase(NvsNamespaceId namespace_id, const char* key) noexcept;

}  // namespace service
