/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_ota.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#if CONFIG_ZGW_OTA_USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static bool copy_version_string(const char* source, char* out, size_t out_len) {
    if (source == NULL || out == NULL || out_len == 0U) {
        return false;
    }

    const size_t source_len = strlen(source);
    if (source_len + 1U > out_len) {
        return false;
    }

    memcpy(out, source, source_len + 1U);
    return true;
}

static void init_https_result(hal_ota_https_result_t* out_result) {
    if (out_result == NULL) {
        return;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->status = HAL_OTA_HTTPS_STATUS_INTERNAL_ERROR;
}

int __attribute__((weak)) hal_ota_platform_perform_https_update(
    const hal_ota_https_request_t* request,
    hal_ota_progress_cb_t progress_cb,
    void* user_ctx,
    hal_ota_https_result_t* out_result) {
#ifdef ESP_PLATFORM
    init_https_result(out_result);
    if (request == NULL || request->url == NULL || request->url[0] == '\0' || out_result == NULL) {
        if (out_result != NULL) {
            out_result->status = HAL_OTA_HTTPS_STATUS_INVALID_ARGUMENT;
        }
        return -1;
    }

    esp_http_client_config_t http_config = {
        .url = request->url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
#if CONFIG_ZGW_OTA_USE_CERT_BUNDLE
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .partial_http_download = true,
        .max_http_request_size = 4096,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        out_result->status = HAL_OTA_HTTPS_STATUS_DOWNLOAD_FAILED;
        return -1;
    }

    esp_app_desc_t image_desc = {0};
    err = esp_https_ota_get_img_desc(handle, &image_desc);
    if (err != ESP_OK) {
        (void)esp_https_ota_abort(handle);
        out_result->status = HAL_OTA_HTTPS_STATUS_VERIFY_FAILED;
        return -1;
    }

    (void)copy_version_string(image_desc.version, out_result->discovered_version, sizeof(out_result->discovered_version));
    if (request->expected_version != NULL && request->expected_version[0] != '\0' &&
        strcmp(request->expected_version, image_desc.version) != 0) {
        (void)esp_https_ota_abort(handle);
        out_result->status = HAL_OTA_HTTPS_STATUS_VERIFY_FAILED;
        return -1;
    }

    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        out_result->bytes_read = (uint32_t)esp_https_ota_get_image_len_read(handle);
        {
            const int image_size = esp_https_ota_get_image_size(handle);
            if (image_size > 0) {
                out_result->image_size = (uint32_t)image_size;
                out_result->image_size_known = true;
            }
        }

        if (progress_cb != NULL) {
            progress_cb(out_result->bytes_read, out_result->image_size, out_result->image_size_known, user_ctx);
        }
    }

    out_result->bytes_read = (uint32_t)esp_https_ota_get_image_len_read(handle);
    {
        const int image_size = esp_https_ota_get_image_size(handle);
        if (image_size > 0) {
            out_result->image_size = (uint32_t)image_size;
            out_result->image_size_known = true;
        }
    }

    if (err != ESP_OK) {
        (void)esp_https_ota_abort(handle);
        out_result->status = (err == ESP_ERR_OTA_VALIDATE_FAILED) ? HAL_OTA_HTTPS_STATUS_VERIFY_FAILED
                                                                  : HAL_OTA_HTTPS_STATUS_DOWNLOAD_FAILED;
        return -1;
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        (void)esp_https_ota_abort(handle);
        out_result->status = HAL_OTA_HTTPS_STATUS_VERIFY_FAILED;
        return -1;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        out_result->status = (err == ESP_ERR_OTA_VALIDATE_FAILED) ? HAL_OTA_HTTPS_STATUS_VERIFY_FAILED
                                                                  : HAL_OTA_HTTPS_STATUS_APPLY_FAILED;
        return -1;
    }

    out_result->status = HAL_OTA_HTTPS_STATUS_OK;
    out_result->reboot_required = true;
    if (progress_cb != NULL) {
        progress_cb(out_result->bytes_read, out_result->image_size, out_result->image_size_known, user_ctx);
    }
    return 0;
#else
    (void)request;
    (void)progress_cb;
    (void)user_ctx;
    init_https_result(out_result);
    if (out_result != NULL) {
        out_result->status = HAL_OTA_HTTPS_STATUS_INTERNAL_ERROR;
    }
    return -1;
#endif
}

int __attribute__((weak)) hal_ota_platform_mark_running_partition_valid(void) {
#ifdef ESP_PLATFORM
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        return -1;
    }

    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running_partition, &ota_state) != ESP_OK) {
        return -1;
    }

    if (ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
        return 0;
    }

    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK ? 0 : -1;
#else
    return 0;
#endif
}

bool __attribute__((weak)) hal_ota_platform_running_partition_pending_verify(void) {
#ifdef ESP_PLATFORM
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        return false;
    }

    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running_partition, &ota_state) != ESP_OK) {
        return false;
    }

    return ota_state == ESP_OTA_IMG_PENDING_VERIFY;
#else
    return false;
#endif
}

int __attribute__((weak)) hal_ota_platform_schedule_restart(uint32_t delay_ms) {
#ifdef ESP_PLATFORM
    if (delay_ms > 0U) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    esp_restart();
    return 0;
#else
    (void)delay_ms;
    return 0;
#endif
}

bool __attribute__((weak)) hal_ota_platform_get_running_version(char* out, size_t out_len) {
#ifdef ESP_PLATFORM
    const esp_app_desc_t* app_description = esp_app_get_description();
    if (app_description == NULL) {
        return false;
    }

    return copy_version_string(app_description->version, out, out_len);
#else
    return copy_version_string("host-test", out, out_len);
#endif
}

int hal_ota_mark_running_partition_valid(void) {
    return hal_ota_platform_mark_running_partition_valid();
}

bool hal_ota_running_partition_pending_verify(void) {
    return hal_ota_platform_running_partition_pending_verify();
}

int hal_ota_schedule_restart(uint32_t delay_ms) {
    return hal_ota_platform_schedule_restart(delay_ms);
}

bool hal_ota_get_running_version(char* out, size_t out_len) {
    return hal_ota_platform_get_running_version(out, out_len);
}

int hal_ota_perform_https_update(
    const hal_ota_https_request_t* request,
    hal_ota_progress_cb_t progress_cb,
    void* user_ctx,
    hal_ota_https_result_t* out_result) {
    return hal_ota_platform_perform_https_update(request, progress_cb, user_ctx, out_result);
}
