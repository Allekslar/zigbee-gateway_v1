/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "secure_storage_port.hpp"

namespace service {

// Plan S5 required change #13 (Encrypted storage foundation): "Add
// production TLS/provisioning key storage interfaces consumed by S6; this
// stage does not generate untracked production certificates or shared
// secrets."
//
// This is an interface-definition sub-slice, matching the plan's own
// wording exactly: every function below is a thin, typed specialization of
// Section 2.8/2.9's generic secure_storage_get_blob/set_blob, scoped to
// the concrete key names this sub-slice adds to the Section 2.7 registry's
// kTlsIdentity and kManufacturingProvisioning entries. No new read/write
// mechanism is introduced -- every guarantee Sections 2.7-2.9 already
// provide (namespace ownership, the plan #12 encryption-verified write
// gate) applies automatically. Nothing in this repository calls any
// function below: S6 -- specifically its "authenticated current/next
// certificate rotation adapter with atomic active-slot selection" (plan
// S6's own text) -- is the actual consumer this interface is defined for.
// This sub-slice generates, reads and writes no real certificate, private
// key or provisioning secret; every value referenced by this module's own
// tests is synthetic placeholder bytes, never anything that resembles
// production key material.
//
// Deliberately out of scope (S6's job, per the plan's own stage split):
// which slot is "active", atomic activation/rollback, certificate
// parsing/validation, SAN/issuer/expiry checks, and populating any of this
// with real values (the two-phase eFuse dry-run/burn workflow that would
// produce a real manufacturing PoP/eFuse record remains
// BLOCKED_SECURITY_PROVISIONING, plan #6-#8).
//
// TLS identity and manufacturing provisioning are both FD-21
// PRESERVE_ON_FACTORY_RESET (Section 2.7 registry) -- no erase function is
// exposed by this module, deliberately: a convenience erase here would
// invite exactly the mistake plan #16/#17's erase enforcement must avoid
// (broad erase touching preserved trust/identity material).

enum class TlsCertificateSlot : uint8_t {
    kCurrent = 0,
    kNext = 1,
};

// Private key / certificate, per slot. DER or PEM bytes -- this module has
// no opinion on encoding, it only stores whatever bytes the (not yet
// implemented) S6 consumer provides.
SecureStorageStatus tls_identity_get_private_key(
    TlsCertificateSlot slot, void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept;
SecureStorageWriteResult tls_identity_set_private_key(
    TlsCertificateSlot slot, const void* key_bytes, uint32_t key_bytes_len) noexcept;
SecureStorageStatus tls_identity_get_certificate(
    TlsCertificateSlot slot, void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept;
SecureStorageWriteResult tls_identity_set_certificate(
    TlsCertificateSlot slot, const void* cert_bytes, uint32_t cert_bytes_len) noexcept;

// Product CA / trust anchor: a single slot, not rotated the way the
// device's own certificate is (FD-21 lists it as its own separate Preserve
// item, distinct from "current/next certificate slots").
SecureStorageStatus tls_identity_get_product_ca(
    void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept;
SecureStorageWriteResult tls_identity_set_product_ca(const void* ca_bytes, uint32_t ca_bytes_len) noexcept;

// Manufacturing proof-of-possession and the eFuse provisioning-template
// record (scripts/efuse_provisioning_template.py, plan #5's schema,
// serialized) -- both single-slot, populated only once real manufacturing
// provisioning (plan #6-#8, BLOCKED_SECURITY_PROVISIONING) exists.
SecureStorageStatus manufacturing_provisioning_get_proof_of_possession(
    void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept;
SecureStorageWriteResult manufacturing_provisioning_set_proof_of_possession(
    const void* pop_bytes, uint32_t pop_bytes_len) noexcept;
SecureStorageStatus manufacturing_provisioning_get_efuse_record(
    void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept;
SecureStorageWriteResult manufacturing_provisioning_set_efuse_record(
    const void* record_bytes, uint32_t record_bytes_len) noexcept;

}  // namespace service
