/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "cert_rotation_state.hpp"

#include "hal_tls_certificate_validator.h"

namespace service {

namespace {

constexpr const char* kCertRotationStateKey = "tls_active_state";

// bit0 = active slot (0=kCurrent, 1=kNext), bit1 = confirmation
// (0=kConfirmed, 1=kPendingConfirmation). Two bits, one u32 -- the exact
// "single atomic key" shape reset_journal_storage_port.hpp already
// established for this codebase.
constexpr uint32_t kActiveSlotBit = 0x1U;
constexpr uint32_t kPendingConfirmationBit = 0x2U;
constexpr uint32_t kValidBitsMask = kActiveSlotBit | kPendingConfirmationBit;

CertRotationState decode(uint32_t raw) noexcept {
    CertRotationState state{};
    state.active_slot =
        (raw & kActiveSlotBit) != 0U ? TlsCertificateSlot::kNext : TlsCertificateSlot::kCurrent;
    state.confirmation = (raw & kPendingConfirmationBit) != 0U ? TlsRotationConfirmation::kPendingConfirmation
                                                                : TlsRotationConfirmation::kConfirmed;
    return state;
}

uint32_t encode(CertRotationState state) noexcept {
    uint32_t raw = 0U;
    if (state.active_slot == TlsCertificateSlot::kNext) {
        raw |= kActiveSlotBit;
    }
    if (state.confirmation == TlsRotationConfirmation::kPendingConfirmation) {
        raw |= kPendingConfirmationBit;
    }
    return raw;
}

TlsCertificateSlot complement(TlsCertificateSlot slot) noexcept {
    return slot == TlsCertificateSlot::kCurrent ? TlsCertificateSlot::kNext : TlsCertificateSlot::kCurrent;
}

}  // namespace

SecureStorageStatus get_cert_rotation_state(CertRotationState* state_out) noexcept {
    uint32_t raw = 0U;
    const SecureStorageStatus status =
        secure_storage_get_u32(NvsNamespaceId::kTlsIdentity, kCertRotationStateKey, &raw);
    if (status != SecureStorageStatus::kAvailable) {
        return status;
    }
    if ((raw & ~kValidBitsMask) != 0U) {
        // Present, but not a well-formed record -- plan #10's own
        // "corrupt" case (same treatment reset_journal_storage_port.hpp's
        // is_valid_state() applies to an out-of-range raw value).
        return SecureStorageStatus::kCorrupt;
    }
    if (state_out != nullptr) {
        *state_out = decode(raw);
    }
    return SecureStorageStatus::kAvailable;
}

CertRotationWriteResult set_cert_rotation_state(CertRotationState state) noexcept {
    const SecureStorageWriteResult result =
        secure_storage_set_u32(NvsNamespaceId::kTlsIdentity, kCertRotationStateKey, encode(state));
    return result == SecureStorageWriteResult::kWritten ? CertRotationWriteResult::kWritten
                                                          : CertRotationWriteResult::kWriteFailed;
}

TlsCertificateSlot cert_rotation_active_slot_for_listener_start() noexcept {
    CertRotationState state{};
    if (get_cert_rotation_state(&state) != SecureStorageStatus::kAvailable) {
        // kNotProvisioned (nothing ever written), kCorrupt or
        // kUnavailable all fail closed to the same pre-#12 default this
        // project already shipped: TlsCertificateSlot::kCurrent.
        return TlsCertificateSlot::kCurrent;
    }
    return state.active_slot;
}

TlsCertificateSlot cert_rotation_staging_slot() noexcept {
    return complement(cert_rotation_active_slot_for_listener_start());
}

CertRotationActivateResult cert_rotation_activate(TlsCertificateSlot new_active_slot) noexcept {
    if (new_active_slot != cert_rotation_staging_slot()) {
        return CertRotationActivateResult::kRejectedNotStagingSlot;
    }
    CertRotationState next_state{};
    next_state.active_slot = new_active_slot;
    next_state.confirmation = TlsRotationConfirmation::kPendingConfirmation;
    return set_cert_rotation_state(next_state) == CertRotationWriteResult::kWritten
               ? CertRotationActivateResult::kActivated
               : CertRotationActivateResult::kWriteFailed;
}

CertRotationConfirmResult cert_rotation_confirm_pending_with_result(bool candidate_still_valid) noexcept {
    CertRotationState state{};
    const SecureStorageStatus status = get_cert_rotation_state(&state);
    if (status == SecureStorageStatus::kNotProvisioned) {
        return CertRotationConfirmResult::kNotRequired;
    }
    if (status != SecureStorageStatus::kAvailable) {
        return CertRotationConfirmResult::kFailed;
    }
    if (state.confirmation != TlsRotationConfirmation::kPendingConfirmation) {
        return CertRotationConfirmResult::kNotRequired;
    }

    CertRotationState next_state{};
    if (candidate_still_valid) {
        next_state.active_slot = state.active_slot;
    } else {
        // Plan #12: "failed verification selects the last confirmed
        // current slot" -- by this module's own invariant (a rotation
        // can only ever be armed by cert_rotation_activate() staging
        // into the complement of an already-confirmed active slot), that
        // previously-confirmed slot is always the complement of the
        // still-pending active slot.
        next_state.active_slot = complement(state.active_slot);
    }
    next_state.confirmation = TlsRotationConfirmation::kConfirmed;

    if (set_cert_rotation_state(next_state) != CertRotationWriteResult::kWritten) {
        return CertRotationConfirmResult::kFailed;
    }
    return candidate_still_valid ? CertRotationConfirmResult::kConfirmed : CertRotationConfirmResult::kRolledBack;
}

CertRotationConfirmResult cert_rotation_confirm_pending(
    const char* expected_dns_san, const char* expected_uri_san) noexcept {
    CertRotationState state{};
    const SecureStorageStatus status = get_cert_rotation_state(&state);
    if (status == SecureStorageStatus::kNotProvisioned) {
        return CertRotationConfirmResult::kNotRequired;
    }
    if (status != SecureStorageStatus::kAvailable) {
        return CertRotationConfirmResult::kFailed;
    }
    if (state.confirmation != TlsRotationConfirmation::kPendingConfirmation) {
        return CertRotationConfirmResult::kNotRequired;
    }

    // `static`, not stack-local: three kTlsCertOrKeyMaxBytes (4096-byte)
    // buffers -- 12KB total -- matches web_server.cpp's own
    // start_production_https() precedent exactly (a real "Guru
    // Meditation Error: Stack protection fault" was observed on real
    // ESP32-C6 hardware the first time that function actually ran with
    // equivalent buffers as stack locals). This function is called from
    // the same "main" task context (WebServer::start(), once per boot),
    // so the identical hazard applies here.
    static uint8_t s_cert_bytes[kTlsCertOrKeyMaxBytes];
    static uint8_t s_key_bytes[kTlsCertOrKeyMaxBytes];
    static uint8_t s_ca_bytes[kTlsCertOrKeyMaxBytes];
    uint32_t cert_len = 0U;
    uint32_t key_len = 0U;
    uint32_t ca_len = 0U;

    const bool material_readable =
        tls_identity_get_certificate(state.active_slot, s_cert_bytes, sizeof(s_cert_bytes), &cert_len) ==
            SecureStorageStatus::kAvailable &&
        tls_identity_get_private_key(state.active_slot, s_key_bytes, sizeof(s_key_bytes), &key_len) ==
            SecureStorageStatus::kAvailable &&
        tls_identity_get_product_ca(s_ca_bytes, sizeof(s_ca_bytes), &ca_len) == SecureStorageStatus::kAvailable;

    // An unreadable active slot is treated the same as a failed
    // validation (fail closed) -- rolls back to the previously confirmed
    // slot rather than leaving the device stuck pending confirmation
    // forever.
    const bool candidate_still_valid =
        material_readable && hal_tls_validate_certificate(
                                  s_cert_bytes, cert_len, s_key_bytes, key_len, s_ca_bytes, ca_len,
                                  expected_dns_san, expected_uri_san) == HAL_TLS_CERT_VALIDATION_VALID;

    return cert_rotation_confirm_pending_with_result(candidate_still_valid);
}

}  // namespace service
