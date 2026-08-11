/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "secure_storage_port.hpp"

#include <cstring>

#include "secure_log_redaction.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "log_tags.h"
#endif

namespace service {

namespace {

bool key_belongs_to_namespace(NvsNamespaceId namespace_id, const char* key) noexcept {
    const NvsNamespaceEntry* owner = find_owning_namespace_for_key(key);
    return owner != nullptr && owner->id == namespace_id;
}

// Plan #14 ("Redact all key/credential material from logs, crash reports
// and completion evidence"): every log line below that mentions a stored
// *value* goes through redact_value_for_log()/format_redacted_value_summary()
// (secure_log_redaction.hpp) -- never the raw bytes. Key NAMES and
// namespace IDs are not secret and are logged directly, matching
// hal_nvs.c's own existing convention (it already logs keys, never
// values). No-ops on host builds, matching this repository's established
// CM_LOGI-style guarded-macro pattern (see e.g. connectivity_manager.cpp).
#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_SECURE_STORAGE;
#define SS_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define SS_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define SS_LOGI(...) ((void)0)
#define SS_LOGW(...) ((void)0)
#endif

void log_redacted_value_written(const char* key, const void* value, uint32_t value_len) {
#ifdef ESP_PLATFORM
    char summary[32];
    format_redacted_value_summary(redact_value_for_log(value, value_len), summary, sizeof(summary));
    SS_LOGI("wrote key='%s' value=%s", key, summary);
#else
    (void)key;
    (void)value;
    (void)value_len;
#endif
}

}  // namespace

SecureStorageStatus secure_storage_classify_raw_status(hal_nvs_status_t raw_status) noexcept {
    switch (raw_status) {
        case HAL_NVS_STATUS_OK:
            return SecureStorageStatus::kAvailable;
        case HAL_NVS_STATUS_NOT_FOUND:
            return SecureStorageStatus::kNotProvisioned;
        case HAL_NVS_STATUS_NO_SPACE:
            return SecureStorageStatus::kCorrupt;
        case HAL_NVS_STATUS_INVALID_ARG:
        case HAL_NVS_STATUS_ERR:
        default:
            return SecureStorageStatus::kUnavailable;
    }
}

SecureStorageStatus secure_storage_downgrade_to_corrupt_if_invalid(
    SecureStorageStatus status, bool content_valid) noexcept {
    if (status == SecureStorageStatus::kAvailable && !content_valid) {
        return SecureStorageStatus::kCorrupt;
    }
    return status;
}

SecureStorageStatus secure_storage_get_u32(
    NvsNamespaceId namespace_id, const char* key, uint32_t* value_out) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("read rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageStatus::kUnavailable;
    }
    const SecureStorageStatus status = secure_storage_classify_raw_status(hal_nvs_get_u32(key, value_out));
    SS_LOGI("read key='%s' status=%u", key, static_cast<unsigned>(status));
    return status;
}

SecureStorageStatus secure_storage_get_str(
    NvsNamespaceId namespace_id, const char* key, char* value_out, uint32_t value_out_capacity) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("read rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageStatus::kUnavailable;
    }
    const SecureStorageStatus status =
        secure_storage_classify_raw_status(hal_nvs_get_str(key, value_out, value_out_capacity));
    SS_LOGI("read key='%s' status=%u", key, static_cast<unsigned>(status));
    return status;
}

SecureStorageStatus secure_storage_get_blob(
    NvsNamespaceId namespace_id,
    const char* key,
    void* value_out,
    uint32_t value_out_capacity,
    uint32_t* value_len_out) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("read rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageStatus::kUnavailable;
    }
    const SecureStorageStatus status =
        secure_storage_classify_raw_status(hal_nvs_get_blob(key, value_out, value_out_capacity, value_len_out));
    SS_LOGI("read key='%s' status=%u", key, static_cast<unsigned>(status));
    return status;
}

bool secure_storage_write_precondition_met(NvsNamespaceId namespace_id) noexcept {
    const NvsNamespaceEntry& entry = find_nvs_namespace_entry(namespace_id);
    if (!entry.encryption_required) {
        return true;
    }
    return hal_security_state_flash_encryption_enabled();
}

SecureStorageWriteResult secure_storage_set_u32(
    NvsNamespaceId namespace_id, const char* key, uint32_t value) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("write rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageWriteResult::kRejectedWrongNamespace;
    }
    if (!secure_storage_write_precondition_met(namespace_id)) {
        SS_LOGW("write rejected: key='%s' namespace requires verified encryption", key);
        return SecureStorageWriteResult::kRejectedEncryptionNotVerified;
    }
    if (hal_nvs_set_u32(key, value) != HAL_NVS_STATUS_OK) {
        SS_LOGW("write failed: key='%s'", key);
        return SecureStorageWriteResult::kWriteFailed;
    }
    // A u32 is still namespace data (e.g. a future session-seed word) --
    // redacted the same as any other value, never printed as a raw
    // integer, matching plan #14's unconditional "all key/credential
    // material" wording rather than assuming scalars are always safe.
    log_redacted_value_written(key, &value, static_cast<uint32_t>(sizeof(value)));
    return SecureStorageWriteResult::kWritten;
}

SecureStorageWriteResult secure_storage_set_str(
    NvsNamespaceId namespace_id, const char* key, const char* value) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("write rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageWriteResult::kRejectedWrongNamespace;
    }
    if (!secure_storage_write_precondition_met(namespace_id)) {
        SS_LOGW("write rejected: key='%s' namespace requires verified encryption", key);
        return SecureStorageWriteResult::kRejectedEncryptionNotVerified;
    }
    if (hal_nvs_set_str(key, value) != HAL_NVS_STATUS_OK) {
        SS_LOGW("write failed: key='%s'", key);
        return SecureStorageWriteResult::kWriteFailed;
    }
    log_redacted_value_written(key, value, value != nullptr ? static_cast<uint32_t>(std::strlen(value)) : 0U);
    return SecureStorageWriteResult::kWritten;
}

SecureStorageWriteResult secure_storage_set_blob(
    NvsNamespaceId namespace_id, const char* key, const void* value, uint32_t value_len) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("write rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageWriteResult::kRejectedWrongNamespace;
    }
    if (!secure_storage_write_precondition_met(namespace_id)) {
        SS_LOGW("write rejected: key='%s' namespace requires verified encryption", key);
        return SecureStorageWriteResult::kRejectedEncryptionNotVerified;
    }
    if (hal_nvs_set_blob(key, value, value_len) != HAL_NVS_STATUS_OK) {
        SS_LOGW("write failed: key='%s'", key);
        return SecureStorageWriteResult::kWriteFailed;
    }
    log_redacted_value_written(key, value, value_len);
    return SecureStorageWriteResult::kWritten;
}

SecureStorageEraseResult secure_storage_erase(NvsNamespaceId namespace_id, const char* key) noexcept {
    if (!key_belongs_to_namespace(namespace_id, key)) {
        SS_LOGW("erase rejected: key='%s' does not belong to the claimed namespace", key);
        return SecureStorageEraseResult::kRejectedWrongNamespace;
    }
    const hal_nvs_status_t raw_status = hal_nvs_erase_key(key);
    if (raw_status == HAL_NVS_STATUS_OK || raw_status == HAL_NVS_STATUS_NOT_FOUND) {
        SS_LOGI("erased key='%s'", key);
        return SecureStorageEraseResult::kErased;
    }
    SS_LOGW("erase failed: key='%s'", key);
    return SecureStorageEraseResult::kEraseFailed;
}

}  // namespace service
