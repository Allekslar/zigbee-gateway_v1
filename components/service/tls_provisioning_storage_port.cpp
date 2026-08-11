/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tls_provisioning_storage_port.hpp"

namespace service {

namespace {

const char* private_key_key(TlsCertificateSlot slot) {
    return slot == TlsCertificateSlot::kCurrent ? "tls_key_cur" : "tls_key_nxt";
}

const char* certificate_key(TlsCertificateSlot slot) {
    return slot == TlsCertificateSlot::kCurrent ? "tls_cert_cur" : "tls_cert_nxt";
}

}  // namespace

SecureStorageStatus tls_identity_get_private_key(
    TlsCertificateSlot slot, void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept {
    return secure_storage_get_blob(
        NvsNamespaceId::kTlsIdentity, private_key_key(slot), value_out, value_out_capacity, value_len_out);
}

SecureStorageWriteResult tls_identity_set_private_key(
    TlsCertificateSlot slot, const void* key_bytes, uint32_t key_bytes_len) noexcept {
    return secure_storage_set_blob(NvsNamespaceId::kTlsIdentity, private_key_key(slot), key_bytes, key_bytes_len);
}

SecureStorageStatus tls_identity_get_certificate(
    TlsCertificateSlot slot, void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept {
    return secure_storage_get_blob(
        NvsNamespaceId::kTlsIdentity, certificate_key(slot), value_out, value_out_capacity, value_len_out);
}

SecureStorageWriteResult tls_identity_set_certificate(
    TlsCertificateSlot slot, const void* cert_bytes, uint32_t cert_bytes_len) noexcept {
    return secure_storage_set_blob(NvsNamespaceId::kTlsIdentity, certificate_key(slot), cert_bytes, cert_bytes_len);
}

SecureStorageStatus tls_identity_get_product_ca(
    void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept {
    return secure_storage_get_blob(NvsNamespaceId::kTlsIdentity, "tls_ca", value_out, value_out_capacity, value_len_out);
}

SecureStorageWriteResult tls_identity_set_product_ca(const void* ca_bytes, uint32_t ca_bytes_len) noexcept {
    return secure_storage_set_blob(NvsNamespaceId::kTlsIdentity, "tls_ca", ca_bytes, ca_bytes_len);
}

SecureStorageStatus manufacturing_provisioning_get_proof_of_possession(
    void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept {
    return secure_storage_get_blob(
        NvsNamespaceId::kManufacturingProvisioning, "mfg_pop", value_out, value_out_capacity, value_len_out);
}

SecureStorageWriteResult manufacturing_provisioning_set_proof_of_possession(
    const void* pop_bytes, uint32_t pop_bytes_len) noexcept {
    return secure_storage_set_blob(NvsNamespaceId::kManufacturingProvisioning, "mfg_pop", pop_bytes, pop_bytes_len);
}

SecureStorageStatus manufacturing_provisioning_get_efuse_record(
    void* value_out, uint32_t value_out_capacity, uint32_t* value_len_out) noexcept {
    return secure_storage_get_blob(
        NvsNamespaceId::kManufacturingProvisioning, "mfg_efuse_rec", value_out, value_out_capacity, value_len_out);
}

SecureStorageWriteResult manufacturing_provisioning_set_efuse_record(
    const void* record_bytes, uint32_t record_bytes_len) noexcept {
    return secure_storage_set_blob(
        NvsNamespaceId::kManufacturingProvisioning, "mfg_efuse_rec", record_bytes, record_bytes_len);
}

}  // namespace service
