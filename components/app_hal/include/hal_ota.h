/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hal_ota_mark_running_partition_valid(void);
bool hal_ota_running_partition_pending_verify(void);
int hal_ota_schedule_restart(uint32_t delay_ms);
bool hal_ota_get_running_version(char* out, size_t out_len);

typedef enum {
    HAL_OTA_HTTPS_STATUS_OK = 0,
    HAL_OTA_HTTPS_STATUS_INVALID_ARGUMENT = 1,
    HAL_OTA_HTTPS_STATUS_DOWNLOAD_FAILED = 2,
    HAL_OTA_HTTPS_STATUS_VERIFY_FAILED = 3,
    HAL_OTA_HTTPS_STATUS_APPLY_FAILED = 4,
    HAL_OTA_HTTPS_STATUS_INTERNAL_ERROR = 5,
} hal_ota_https_status_t;

typedef struct {
    const char* url;
    const char* expected_version;
    const char* expected_project_name;
} hal_ota_https_request_t;

typedef void (*hal_ota_progress_cb_t)(
    uint32_t bytes_read,
    uint32_t image_size,
    bool image_size_known,
    void* user_ctx);

typedef struct {
    hal_ota_https_status_t status;
    bool reboot_required;
    uint32_t bytes_read;
    uint32_t image_size;
    bool image_size_known;
    char discovered_version[32];
    char discovered_project_name[32];
} hal_ota_https_result_t;

int hal_ota_perform_https_update(
    const hal_ota_https_request_t* request,
    hal_ota_progress_cb_t progress_cb,
    void* user_ctx,
    hal_ota_https_result_t* out_result);

#ifdef __cplusplus
}
#endif
