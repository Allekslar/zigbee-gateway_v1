/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_u32_written)(void* context, const char* key, uint32_t value);
} hal_nvs_callbacks_t;

typedef enum {
    HAL_NVS_STATUS_OK = 0,
    HAL_NVS_STATUS_INVALID_ARG = -1,
    HAL_NVS_STATUS_NOT_FOUND = -2,
    HAL_NVS_STATUS_NO_SPACE = -3,
    HAL_NVS_STATUS_ERR = -4,
} hal_nvs_status_t;

hal_nvs_status_t hal_nvs_init(void);
hal_nvs_status_t hal_nvs_register_callbacks(const hal_nvs_callbacks_t* callbacks, void* context);
hal_nvs_status_t hal_nvs_set_u32(const char* key, uint32_t value);
hal_nvs_status_t hal_nvs_get_u32(const char* key, uint32_t* value_out);
hal_nvs_status_t hal_nvs_set_str(const char* key, const char* value);
hal_nvs_status_t hal_nvs_get_str(const char* key, char* value_out, uint32_t value_out_capacity);
hal_nvs_status_t hal_nvs_set_blob(const char* key, const void* value, uint32_t value_len);
hal_nvs_status_t hal_nvs_get_blob(
    const char* key,
    void* value_out,
    uint32_t value_out_capacity,
    uint32_t* value_len_out);

// Erases a single key-value pair (plan S5 required change #11's "erase
// plaintext" step; docs/security/PRODUCTION_HARDENING.md Section 2.10).
// Type-agnostic -- works regardless of whether `key` was last written via
// set_u32/set_str/set_blob. Returns HAL_NVS_STATUS_OK if the key existed
// and was erased, HAL_NVS_STATUS_NOT_FOUND if it did not exist (this is
// not treated as a failure by this function itself -- callers that need
// idempotent-erase semantics should treat NOT_FOUND as success too, which
// service::secure_storage_erase() does).
hal_nvs_status_t hal_nvs_erase_key(const char* key);

#ifdef __cplusplus
}
#endif
