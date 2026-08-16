/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "cert_rotation_state.hpp"
#include "hal_security_state_test.h"

int main() {
    // kTlsIdentity is encryption_required (Section 2.7 registry) --
    // every write in this file needs the host mock reporting encryption
    // as verified-active, matching test_tls_provisioning_storage_port.cpp's
    // own established convention for the same namespace.
    hal_security_state_set_mock_flash_encryption_enabled(true);

    // Fresh (never-written) state: kNotProvisioned, and every real caller
    // falls back to the pre-#12 default (active = kCurrent).
    {
        service::CertRotationState state{};
        assert(
            service::get_cert_rotation_state(&state) == service::SecureStorageStatus::kNotProvisioned);
        assert(
            service::cert_rotation_active_slot_for_listener_start() == service::TlsCertificateSlot::kCurrent);
        assert(service::cert_rotation_staging_slot() == service::TlsCertificateSlot::kNext);
    }

    // set_cert_rotation_state()/get_cert_rotation_state() round trip for
    // all 4 (active_slot, confirmation) combinations.
    {
        const service::TlsCertificateSlot slots[2] = {
            service::TlsCertificateSlot::kCurrent, service::TlsCertificateSlot::kNext};
        const service::TlsRotationConfirmation confirmations[2] = {
            service::TlsRotationConfirmation::kConfirmed, service::TlsRotationConfirmation::kPendingConfirmation};
        for (const auto slot : slots) {
            for (const auto confirmation : confirmations) {
                service::CertRotationState written{};
                written.active_slot = slot;
                written.confirmation = confirmation;
                assert(service::set_cert_rotation_state(written) == service::CertRotationWriteResult::kWritten);

                service::CertRotationState read_back{};
                assert(service::get_cert_rotation_state(&read_back) == service::SecureStorageStatus::kAvailable);
                assert(read_back.active_slot == slot);
                assert(read_back.confirmation == confirmation);
            }
        }
    }

    // cert_rotation_activate() from a confirmed, never-rotated steady
    // state: activates the staging slot (kNext) with confirmation
    // pending.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);

        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kActivated);
        assert(service::cert_rotation_active_slot_for_listener_start() == service::TlsCertificateSlot::kNext);
        assert(service::cert_rotation_staging_slot() == service::TlsCertificateSlot::kCurrent);

        service::CertRotationState after{};
        assert(service::get_cert_rotation_state(&after) == service::SecureStorageStatus::kAvailable);
        assert(after.confirmation == service::TlsRotationConfirmation::kPendingConfirmation);
    }

    // cert_rotation_activate() rejects a slot that is NOT the current
    // staging slot -- e.g. re-"activating" the slot that is already
    // active (a caller logic error, not a valid rotation) -- and leaves
    // state unchanged.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);

        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kCurrent) ==
            service::CertRotationActivateResult::kRejectedNotStagingSlot);

        service::CertRotationState after{};
        assert(service::get_cert_rotation_state(&after) == service::SecureStorageStatus::kAvailable);
        assert(after.active_slot == service::TlsCertificateSlot::kCurrent);
        assert(after.confirmation == service::TlsRotationConfirmation::kConfirmed);
    }

    // While a rotation is pending, a second activate() call (even
    // targeting what LOOKS like a fresh staging slot from the caller's
    // perspective) is rejected, because the real staging slot right now
    // is the complement of the PENDING active slot, not the original
    // one -- prevents re-arming mid-rotation.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kActivated);

        // Staging slot is now kCurrent (complement of the pending kNext)
        // -- activating kNext again (the one already pending) must be
        // rejected.
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kRejectedNotStagingSlot);
    }

    // cert_rotation_confirm_pending_with_result(true): a pending
    // rotation's active slot re-validates successfully -> kConfirmed,
    // active slot unchanged, confirmation flips to kConfirmed.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kActivated);

        assert(
            service::cert_rotation_confirm_pending_with_result(true) ==
            service::CertRotationConfirmResult::kConfirmed);

        service::CertRotationState after{};
        assert(service::get_cert_rotation_state(&after) == service::SecureStorageStatus::kAvailable);
        assert(after.active_slot == service::TlsCertificateSlot::kNext);
        assert(after.confirmation == service::TlsRotationConfirmation::kConfirmed);
    }

    // cert_rotation_confirm_pending_with_result(false): re-validation
    // fails -> kRolledBack, active slot reverts to the previously
    // confirmed slot (kCurrent), confirmation flips back to kConfirmed
    // for that slot (not left pending).
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kActivated);

        assert(
            service::cert_rotation_confirm_pending_with_result(false) ==
            service::CertRotationConfirmResult::kRolledBack);

        service::CertRotationState after{};
        assert(service::get_cert_rotation_state(&after) == service::SecureStorageStatus::kAvailable);
        assert(after.active_slot == service::TlsCertificateSlot::kCurrent);
        assert(after.confirmation == service::TlsRotationConfirmation::kConfirmed);
        assert(service::cert_rotation_active_slot_for_listener_start() == service::TlsCertificateSlot::kCurrent);
    }

    // Nothing pending (steady confirmed state, or never-provisioned) ->
    // kNotRequired, regardless of the injected outcome, and no state
    // change.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kNext;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);

        assert(
            service::cert_rotation_confirm_pending_with_result(true) ==
            service::CertRotationConfirmResult::kNotRequired);
        assert(
            service::cert_rotation_confirm_pending_with_result(false) ==
            service::CertRotationConfirmResult::kNotRequired);

        service::CertRotationState after{};
        assert(service::get_cert_rotation_state(&after) == service::SecureStorageStatus::kAvailable);
        assert(after.active_slot == service::TlsCertificateSlot::kNext);
        assert(after.confirmation == service::TlsRotationConfirmation::kConfirmed);
    }

    // Multi-rotation cycle sanity: confirm(true) on one rotation, then
    // activate + roll back the NEXT rotation -- must revert to the slot
    // that was actually just confirmed, not the original starting slot.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);

        // Rotation 1: Current -> Next, confirmed.
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kActivated);
        assert(
            service::cert_rotation_confirm_pending_with_result(true) ==
            service::CertRotationConfirmResult::kConfirmed);
        assert(service::cert_rotation_active_slot_for_listener_start() == service::TlsCertificateSlot::kNext);

        // Rotation 2: Next -> Current, rolled back -> must land back on
        // kNext (the slot rotation 1 just confirmed), not some stale
        // notion of the very first slot.
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kCurrent) ==
            service::CertRotationActivateResult::kActivated);
        assert(
            service::cert_rotation_confirm_pending_with_result(false) ==
            service::CertRotationConfirmResult::kRolledBack);
        assert(service::cert_rotation_active_slot_for_listener_start() == service::TlsCertificateSlot::kNext);
    }

    // cert_rotation_confirm_pending() real entry point: on host,
    // hal_tls_validate_certificate() always fails closed (no real
    // mbedtls available -- see hal_tls_certificate_validator.h's own
    // header comment), and no TLS material was ever written to the
    // active slot in this test either, so this must always roll back --
    // the "failure/rollback path is host-tested for real, the success
    // path needs a real idf.py build" split this project applies
    // everywhere real X.509 logic is involved.
    {
        service::CertRotationState steady{};
        steady.active_slot = service::TlsCertificateSlot::kCurrent;
        steady.confirmation = service::TlsRotationConfirmation::kConfirmed;
        assert(service::set_cert_rotation_state(steady) == service::CertRotationWriteResult::kWritten);
        assert(
            service::cert_rotation_activate(service::TlsCertificateSlot::kNext) ==
            service::CertRotationActivateResult::kActivated);

        assert(
            service::cert_rotation_confirm_pending("zigbee-gateway-abcdef.local", "urn:zgw:abcdef123456") ==
            service::CertRotationConfirmResult::kRolledBack);
        assert(service::cert_rotation_active_slot_for_listener_start() == service::TlsCertificateSlot::kCurrent);
    }

    // Null-pointer safety: get_cert_rotation_state(nullptr) still
    // reports status correctly without touching anything.
    {
        assert(service::get_cert_rotation_state(nullptr) == service::SecureStorageStatus::kAvailable);
    }

    hal_security_state_reset_mock_flash_encryption_enabled();
    return 0;
}
