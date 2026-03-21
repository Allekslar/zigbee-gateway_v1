/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_RCP_HTTPS_STATUS_OK = 0,
    HAL_RCP_HTTPS_STATUS_INVALID_ARGUMENT = 1,
    HAL_RCP_HTTPS_STATUS_TRANSPORT_FAILED = 2,
    HAL_RCP_HTTPS_STATUS_VERIFY_FAILED = 3,
    HAL_RCP_HTTPS_STATUS_APPLY_FAILED = 4,
    HAL_RCP_HTTPS_STATUS_PROBE_FAILED = 5,
    HAL_RCP_HTTPS_STATUS_RECOVERY_FAILED = 6,
    HAL_RCP_HTTPS_STATUS_INTERNAL_ERROR = 7,
} hal_rcp_https_status_t;

typedef struct {
    const char* url;
    const char* expected_sha256_hex;
    const char* expected_version;
} hal_rcp_https_request_t;

typedef struct {
    hal_rcp_https_status_t status;
    uint32_t bytes_read;
    uint32_t last_esp_err;
    uint32_t last_tls_error;
    int32_t esp_tls_error_code;
    int32_t esp_tls_flags;
    int32_t socket_errno;
    int32_t http_status_code;
    uint8_t failure_stage;
    char discovered_version[32];
} hal_rcp_https_result_t;

bool hal_rcp_backend_available(void);
bool hal_rcp_get_backend_name(char* out, size_t out_len);
bool hal_rcp_get_running_version(char* out, size_t out_len);
int hal_rcp_prepare_for_update(void);
int hal_rcp_update_begin(void);
int hal_rcp_update_write(const uint8_t* data, uint32_t len);
int hal_rcp_update_end(void);
int hal_rcp_recover_after_update(bool update_applied);
int hal_rcp_perform_https_update(const hal_rcp_https_request_t* request, hal_rcp_https_result_t* out_result);

#ifdef __cplusplus
}
#endif
