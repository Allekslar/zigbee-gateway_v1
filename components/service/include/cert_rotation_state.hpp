/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "secure_storage_port.hpp"
#include "tls_provisioning_storage_port.hpp"

namespace service {

// Plan S6 "HTTPS and sessions" #12 / FD-17: "Certificate storage has
// encrypted `current` and `next` slots plus one atomic active-slot
// reference. Rotation is authenticated, requires physical presence,
// validates key/certificate/SAN/issuer/expiry, starts a bounded local
// verification using `next`, atomically switches the active reference
// only after validation, and retains the previous confirmed slot until
// one successful reboot/post-activation check completes... Power loss or
// failed verification selects the last confirmed `current` slot before
// listener enablement."
//
// tls_provisioning_storage_port.hpp (plan S5 #13) already gives this
// project two PHYSICAL storage slots (kCurrent/kNext). What was missing
// -- named explicitly as "S6's job" in that module's own header comment
// -- is the piece this file adds: a small, separately-persisted ACTIVE-
// SLOT REFERENCE, decoupled from the physical slots themselves, so
// "atomically switch the active reference" can be a single u32 NVS write
// (inherits atomicity from ESP-IDF's own NVS commit semantics -- the
// exact same reasoning reset_journal_storage_port.hpp already
// established for this codebase) rather than copying cert/key bytes
// between slots (which could tear across a power loss).
//
// This project's rotation design always writes a new candidate into
// whichever slot is NOT currently active (cert_rotation_staging_slot()),
// so the "previous confirmed slot" plan #12 wants retained is always
// exactly the OTHER slot from whichever is now active -- no separate
// field is needed to track it, only the confirmation flag below.
//
// Default state (kNotProvisioned, before any rotation has ever run):
// active slot = kCurrent, confirmation = kConfirmed -- exactly
// Section 2.7's original, pre-#12 behavior (start_production_https()
// always read kCurrent unconditionally), so this module is purely
// additive: no existing device/build path changes unless a real
// rotation is actually performed.
//
// Host-testability boundary, matching hal_tls_certificate_validator.h's
// own established precedent exactly: every function below whose logic
// is pure state-machine bookkeeping is fully host-testable (backed by
// Section 2.8's secure_storage_port host mock, same as
// reset_journal_storage_port.hpp). Only cert_rotation_confirm_pending()
// itself calls the real hal_tls_validate_certificate() -- on host that
// call always fails closed (returns
// HAL_TLS_CERT_VALIDATION_CERTIFICATE_PARSE_FAILED unconditionally, per
// that module's own host branch), so host tests can exercise the
// rollback path for real but never the "re-confirmed successfully" path
// -- the same "success path needs a real idf.py build, failure/rollback
// path is host-tested" split this project already applies everywhere
// real X.509 logic is involved.

enum class TlsRotationConfirmation : uint8_t {
    kConfirmed = 0,
    kPendingConfirmation = 1,
};

struct CertRotationState {
    TlsCertificateSlot active_slot = TlsCertificateSlot::kCurrent;
    TlsRotationConfirmation confirmation = TlsRotationConfirmation::kConfirmed;
};

// Raw port, mirrors reset_journal_storage_port.hpp's own two-function
// shape exactly. kNotProvisioned (nothing ever written) is a normal,
// expected status on any device that has never rotated -- callers that
// want a concrete default should use
// cert_rotation_active_slot_for_listener_start() below instead of
// treating kNotProvisioned as an error.
SecureStorageStatus get_cert_rotation_state(CertRotationState* state_out) noexcept;

enum class CertRotationWriteResult : uint8_t {
    kWritten = 0,
    kWriteFailed = 1,
};

// Writes `state` as a single atomic operation (see the module-level note
// above).
CertRotationWriteResult set_cert_rotation_state(CertRotationState state) noexcept;

// The slot a real listener-start call site should load right now.
// Always succeeds, always fails closed to TlsCertificateSlot::kCurrent
// (matching the pre-#12 default) on any non-kAvailable read
// (kNotProvisioned, kCorrupt, kUnavailable) -- a storage read failure
// here must never surface as "no certificate available at all" when a
// perfectly good kCurrent slot might still be readable on its own.
TlsCertificateSlot cert_rotation_active_slot_for_listener_start() noexcept;

// The slot a NEW rotation candidate must be written into: always the
// complement of whatever cert_rotation_active_slot_for_listener_start()
// currently returns. This project's rotation design never lets a
// candidate overwrite the slot currently in use.
TlsCertificateSlot cert_rotation_staging_slot() noexcept;

// Activates `new_active_slot` with confirmation =
// kPendingConfirmation. Plan #12: "atomically switches the active
// reference only after validation" -- this function IS that switch; the
// caller (the rotation route) is responsible for having already
// validated the candidate material (offline X.509 checks at minimum)
// before calling this. Rejects `new_active_slot` if it is not the
// current staging slot (i.e. refuses to "activate" the slot that is
// already active) -- a caller race or logic error must not silently
// re-arm a rotation that never actually happened.
enum class CertRotationActivateResult : uint8_t {
    kActivated = 0,
    kRejectedNotStagingSlot = 1,
    kWriteFailed = 2,
};
CertRotationActivateResult cert_rotation_activate(TlsCertificateSlot new_active_slot) noexcept;

enum class CertRotationConfirmResult : uint8_t {
    // No rotation is pending (steady state, or nothing has ever been
    // written) -- nothing to do.
    kNotRequired = 0,
    // A pending rotation's active slot re-validated successfully;
    // confirmation flipped to kConfirmed. This is plan #12's "one
    // successful reboot/post-activation check completes."
    kConfirmed = 1,
    // A pending rotation's active slot failed re-validation (or was
    // unreadable); active slot reverted to its complement (the
    // previously-confirmed slot) and confirmation flipped to kConfirmed
    // for that slot. This is plan #12's "power loss or failed
    // verification selects the last confirmed current slot."
    kRolledBack = 2,
    // The state itself could not be read/written (storage failure) --
    // distinct from kRolledBack: no state change was possible at all.
    // Callers should still fall back to
    // cert_rotation_active_slot_for_listener_start()'s own fail-closed
    // default afterward.
    kFailed = 3,
};

// Pure state-machine core, host-testable: given the ALREADY-COMPUTED
// outcome of re-validating the current pending active slot's material
// (`candidate_still_valid`), decides and persists the resulting state.
// Split out from cert_rotation_confirm_pending() below for the exact
// same reason this project splits every other real-crypto-dependent
// decision from its own host-testable policy core (commissioning_
// window.hpp's explicit-now_ms pattern is the general precedent this
// follows: inject the input that cannot be host-computed, keep
// everything downstream of it pure and testable).
CertRotationConfirmResult cert_rotation_confirm_pending_with_result(bool candidate_still_valid) noexcept;

// Real entry point: if a rotation is pending confirmation, re-reads the
// now-active slot's own certificate/private key plus the product CA
// from storage, re-validates via hal_tls_validate_certificate() against
// `expected_dns_san`/`expected_uri_san` (same shape
// hal_tls_certificate_validator.h's own function takes -- callers
// already compute these once per boot for their own
// start_production_https()-equivalent validation, e.g.
// web_server.cpp's own dns_san/uri_san locals), and calls
// cert_rotation_confirm_pending_with_result() with the real outcome. A
// storage read failure for the active slot's own material is treated as
// "not still valid" (fail closed, same as any other unreadable-slot
// case in this project).
CertRotationConfirmResult cert_rotation_confirm_pending(
    const char* expected_dns_san, const char* expected_uri_san) noexcept;

}  // namespace service
