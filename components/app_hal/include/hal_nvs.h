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

#ifdef __cplusplus
}
#endif
