/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_rcp.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "mbedtls/sha256.h"
#if CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif
#endif

#ifdef ESP_PLATFORM
#if CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA
extern const char ota_server_root_ca_pem_start[] asm("_binary_ota_server_root_ca_pem_start");
extern const char ota_server_root_ca_pem_end[] asm("_binary_ota_server_root_ca_pem_end");
#endif
#endif

// Weak hooks for an optional platform-specific external RCP updater.
// On the current ESP32-C6-DevKitC target, Zigbee uses the native 15.4 radio
// and these hooks are expected to remain unconfigured.
bool __attribute__((weak)) hal_rcp_stack_backend_available(void) {
#ifdef ESP_PLATFORM
    return false;
#else
    return true;
#endif
}

bool __attribute__((weak)) hal_rcp_stack_get_backend_name(char* out, size_t out_len) {
    if (out == NULL || out_len == 0U) {
        return false;
    }
#ifdef ESP_PLATFORM
    if (out_len < 13U) {
        return false;
    }
    memcpy(out, "unconfigured", 13U);
#else
    if (out_len < 10U) {
        return false;
    }
    memcpy(out, "host-mock", 10U);
#endif
    return true;
}

bool __attribute__((weak)) hal_rcp_stack_get_running_version(char* out, size_t out_len) {
#ifdef ESP_PLATFORM
    (void)out;
    (void)out_len;
    return false;
#else
    if (out == NULL || out_len < 9U) {
        return false;
    }
    memcpy(out, "host-rcp", 9U);
    return true;
#endif
}

int __attribute__((weak)) hal_rcp_stack_prepare_for_update(void) {
    return 0;
}

int __attribute__((weak)) hal_rcp_stack_update_begin(void) {
#ifdef ESP_PLATFORM
    return -1;
#else
    return 0;
#endif
}

int __attribute__((weak)) hal_rcp_stack_update_write(const uint8_t* data, uint32_t len) {
#ifdef ESP_PLATFORM
    (void)data;
    (void)len;
    return -1;
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

int __attribute__((weak)) hal_rcp_stack_update_end(void) {
#ifdef ESP_PLATFORM
    return -1;
#else
    return 0;
#endif
}

int __attribute__((weak)) hal_rcp_stack_recover_after_update(bool update_applied) {
    (void)update_applied;
    return 0;
}

static void hal_rcp_init_https_result(hal_rcp_https_result_t* out_result) {
    if (out_result == NULL) {
        return;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->status = HAL_RCP_HTTPS_STATUS_INTERNAL_ERROR;
    out_result->http_status_code = -1;
    out_result->socket_errno = -1;
}

static bool hal_rcp_is_valid_sha256_hex(const char* value) {
    if (value == NULL || *value == '\0') {
        return true;
    }
    if (strlen(value) != 64U) {
        return false;
    }
    for (size_t i = 0; i < 64U; ++i) {
        if (isxdigit((unsigned char)value[i]) == 0) {
            return false;
        }
    }
    return true;
}

#ifdef ESP_PLATFORM
static int hal_rcp_hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + (value - 'a');
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + (value - 'A');
    }
    return -1;
}
#endif

 #ifdef ESP_PLATFORM
static bool hal_rcp_sha256_matches_hex(const unsigned char* digest, size_t digest_len, const char* expected_hex) {
    if (digest == NULL || digest_len != 32U || expected_hex == NULL || *expected_hex == '\0') {
        return true;
    }

    for (size_t i = 0; i < digest_len; ++i) {
        const int hi = hal_rcp_hex_nibble(expected_hex[i * 2U]);
        const int lo = hal_rcp_hex_nibble(expected_hex[(i * 2U) + 1U]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        if (digest[i] != (unsigned char)((hi << 4) | lo)) {
            return false;
        }
    }

    return true;
}
#endif

bool hal_rcp_backend_available(void) {
    return hal_rcp_stack_backend_available();
}

bool hal_rcp_get_backend_name(char* out, size_t out_len) {
    return hal_rcp_stack_get_backend_name(out, out_len);
}

bool hal_rcp_get_running_version(char* out, size_t out_len) {
    return hal_rcp_stack_get_running_version(out, out_len);
}

int hal_rcp_prepare_for_update(void) {
    return hal_rcp_stack_prepare_for_update();
}

int hal_rcp_update_begin(void) {
    return hal_rcp_stack_update_begin();
}

int hal_rcp_update_write(const uint8_t* data, uint32_t len) {
    return hal_rcp_stack_update_write(data, len);
}

int hal_rcp_update_end(void) {
    return hal_rcp_stack_update_end();
}

int hal_rcp_recover_after_update(bool update_applied) {
    return hal_rcp_stack_recover_after_update(update_applied);
}

#ifdef ESP_PLATFORM
static bool hal_rcp_url_has_prefix(const char* url, const char* prefix) {
    if (url == NULL || prefix == NULL) {
        return false;
    }

    const size_t prefix_len = strlen(prefix);
    return strncmp(url, prefix, prefix_len) == 0;
}

static bool hal_rcp_configure_tls_trust(esp_http_client_config_t* http_config) {
    if (http_config == NULL) {
        return false;
    }

#if CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE
    http_config->crt_bundle_attach = esp_crt_bundle_attach;
    return true;
#elif CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA
    const size_t cert_len = (size_t)(ota_server_root_ca_pem_end - ota_server_root_ca_pem_start);
    if (cert_len <= 1U || strstr(ota_server_root_ca_pem_start, "BEGIN CERTIFICATE") == NULL) {
        return false;
    }
    http_config->cert_pem = ota_server_root_ca_pem_start;
    return true;
#else
    return false;
#endif
}

typedef struct {
    esp_http_client_handle_t client;
    esp_err_t last_tls_error;
    int esp_tls_error_code;
    int esp_tls_flags;
    int socket_errno;
    int http_status_code;
} hal_rcp_http_diag_t;

static esp_err_t hal_rcp_http_event_handler(esp_http_client_event_t* evt) {
    if (evt == NULL) {
        return ESP_OK;
    }

    hal_rcp_http_diag_t* diag = (hal_rcp_http_diag_t*)evt->user_data;
    if (diag == NULL) {
        return ESP_OK;
    }

    diag->client = evt->client;
    if (evt->client != NULL) {
        diag->http_status_code = esp_http_client_get_status_code(evt->client);
    }

    if (evt->event_id == HTTP_EVENT_ERROR && evt->data != NULL) {
        const esp_tls_last_error_t* error = (const esp_tls_last_error_t*)evt->data;
        diag->last_tls_error = error->last_error;
        diag->esp_tls_error_code = error->esp_tls_error_code;
        diag->esp_tls_flags = error->esp_tls_flags;
        diag->socket_errno = (evt->client != NULL) ? esp_http_client_get_errno(evt->client) : -1;
    }

    if (evt->event_id == HTTP_EVENT_DISCONNECTED && evt->client != NULL) {
        int esp_tls_error_code = 0;
        int esp_tls_flags = 0;
        diag->socket_errno = esp_http_client_get_errno(evt->client);
        diag->last_tls_error = esp_http_client_get_and_clear_last_tls_error(
            evt->client, &esp_tls_error_code, &esp_tls_flags);
        diag->esp_tls_error_code = esp_tls_error_code;
        diag->esp_tls_flags = esp_tls_flags;
    }

    return ESP_OK;
}
#endif

int hal_rcp_perform_https_update(const hal_rcp_https_request_t* request, hal_rcp_https_result_t* out_result) {
    hal_rcp_init_https_result(out_result);
    if (request == NULL || out_result == NULL || request->url == NULL || request->url[0] == '\0' ||
        !hal_rcp_is_valid_sha256_hex(request->expected_sha256_hex)) {
        if (out_result != NULL) {
            out_result->status = HAL_RCP_HTTPS_STATUS_INVALID_ARGUMENT;
        }
        return -1;
    }

#ifdef ESP_PLATFORM
    if (!hal_rcp_url_has_prefix(request->url, "https://")) {
#if defined(CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING) && CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING
        if (!hal_rcp_url_has_prefix(request->url, "http://")) {
            out_result->status = HAL_RCP_HTTPS_STATUS_INVALID_ARGUMENT;
            return -1;
        }
#else
        out_result->status = HAL_RCP_HTTPS_STATUS_INVALID_ARGUMENT;
        return -1;
#endif
    }

    if (hal_rcp_prepare_for_update() != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_RECOVERY_FAILED;
        return -1;
    }

    esp_http_client_config_t http_config = {
        .url = request->url,
        .timeout_ms = 30000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .event_handler = hal_rcp_http_event_handler,
    };
    hal_rcp_http_diag_t diag = {
        .client = NULL,
        .last_tls_error = ESP_OK,
        .esp_tls_error_code = 0,
        .esp_tls_flags = 0,
        .socket_errno = -1,
        .http_status_code = -1,
    };
    http_config.user_data = &diag;

    if (hal_rcp_url_has_prefix(request->url, "https://") && !hal_rcp_configure_tls_trust(&http_config)) {
        out_result->status = HAL_RCP_HTTPS_STATUS_INTERNAL_ERROR;
        (void)hal_rcp_recover_after_update(false);
        return -1;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        out_result->status = HAL_RCP_HTTPS_STATUS_INTERNAL_ERROR;
        out_result->failure_stage = 1U;
        (void)hal_rcp_recover_after_update(false);
        return -1;
    }

    diag.client = client;
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    int status = -1;
    unsigned char digest[32] = {0};
    uint8_t buffer[1024];

    if (hal_rcp_update_begin() != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_APPLY_FAILED;
        out_result->failure_stage = 1U;
        goto cleanup;
    }

    {
        const esp_err_t open_err = esp_http_client_open(client, 0);
        if (open_err != ESP_OK) {
            out_result->status = HAL_RCP_HTTPS_STATUS_TRANSPORT_FAILED;
            out_result->last_esp_err = (uint32_t)open_err;
            out_result->last_tls_error = (uint32_t)diag.last_tls_error;
            out_result->esp_tls_error_code = diag.esp_tls_error_code;
            out_result->esp_tls_flags = diag.esp_tls_flags;
            out_result->socket_errno = diag.socket_errno;
            out_result->http_status_code = diag.http_status_code;
            out_result->failure_stage = 2U;
            goto cleanup;
        }
    }

    for (;;) {
        const int read_len = esp_http_client_read(client, (char*)buffer, sizeof(buffer));
        if (read_len < 0) {
            out_result->status = HAL_RCP_HTTPS_STATUS_TRANSPORT_FAILED;
            out_result->last_esp_err = ESP_ERR_HTTP_FETCH_HEADER;
            out_result->last_tls_error = (uint32_t)diag.last_tls_error;
            out_result->esp_tls_error_code = diag.esp_tls_error_code;
            out_result->esp_tls_flags = diag.esp_tls_flags;
            out_result->socket_errno = diag.socket_errno;
            out_result->http_status_code = esp_http_client_get_status_code(client);
            out_result->failure_stage = 3U;
            goto cleanup;
        }
        if (read_len == 0) {
            break;
        }

        mbedtls_sha256_update(&sha_ctx, buffer, (size_t)read_len);
        if (hal_rcp_update_write(buffer, (uint32_t)read_len) != 0) {
            out_result->status = HAL_RCP_HTTPS_STATUS_APPLY_FAILED;
            out_result->http_status_code = esp_http_client_get_status_code(client);
            out_result->failure_stage = 4U;
            goto cleanup;
        }
        out_result->bytes_read += (uint32_t)read_len;
    }

    if (hal_rcp_update_end() != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_APPLY_FAILED;
        out_result->failure_stage = 5U;
        goto cleanup;
    }

    mbedtls_sha256_finish(&sha_ctx, digest);
    if (!hal_rcp_sha256_matches_hex(digest, sizeof(digest), request->expected_sha256_hex)) {
        out_result->status = HAL_RCP_HTTPS_STATUS_VERIFY_FAILED;
        out_result->failure_stage = 5U;
        goto cleanup;
    }

    if (!hal_rcp_get_running_version(out_result->discovered_version, sizeof(out_result->discovered_version))) {
        out_result->status = HAL_RCP_HTTPS_STATUS_PROBE_FAILED;
        out_result->failure_stage = 5U;
        goto cleanup;
    }

    if (request->expected_version != NULL && request->expected_version[0] != '\0' &&
        strcmp(request->expected_version, out_result->discovered_version) != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_PROBE_FAILED;
        out_result->failure_stage = 5U;
        goto cleanup;
    }

    if (hal_rcp_recover_after_update(true) != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_RECOVERY_FAILED;
        out_result->failure_stage = 5U;
        goto cleanup;
    }

    out_result->status = HAL_RCP_HTTPS_STATUS_OK;
    out_result->http_status_code = esp_http_client_get_status_code(client);
    status = 0;

cleanup:
    if (status != 0) {
        (void)hal_rcp_recover_after_update(false);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    mbedtls_sha256_free(&sha_ctx);
    return status;
#else
    if (hal_rcp_prepare_for_update() != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_RECOVERY_FAILED;
        return -1;
    }
    if (hal_rcp_update_begin() != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_APPLY_FAILED;
        return -1;
    }
    out_result->bytes_read = (uint32_t)strlen(request->url);
    if (hal_rcp_update_write((const uint8_t*)request->url, out_result->bytes_read) != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_APPLY_FAILED;
        (void)hal_rcp_recover_after_update(false);
        return -1;
    }
    if (hal_rcp_update_end() != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_APPLY_FAILED;
        (void)hal_rcp_recover_after_update(false);
        return -1;
    }
    if (!hal_rcp_get_running_version(out_result->discovered_version, sizeof(out_result->discovered_version))) {
        out_result->status = HAL_RCP_HTTPS_STATUS_PROBE_FAILED;
        (void)hal_rcp_recover_after_update(false);
        return -1;
    }
    if (request->expected_version != NULL && request->expected_version[0] != '\0' &&
        strcmp(request->expected_version, out_result->discovered_version) != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_PROBE_FAILED;
        (void)hal_rcp_recover_after_update(false);
        return -1;
    }
    if (hal_rcp_recover_after_update(true) != 0) {
        out_result->status = HAL_RCP_HTTPS_STATUS_RECOVERY_FAILED;
        return -1;
    }
    out_result->status = HAL_RCP_HTTPS_STATUS_OK;
    return 0;
#endif
}
