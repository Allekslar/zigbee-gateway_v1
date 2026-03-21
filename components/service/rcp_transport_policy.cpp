/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "rcp_transport_policy.hpp"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <cstring>

namespace {

#ifndef CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING
#define CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING 0
#endif

#ifndef CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE
#define CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE 0
#endif

#ifndef CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA
#define CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA 0
#endif

#ifdef ESP_PLATFORM
#if CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA
extern const char ota_server_root_ca_pem_start[] asm("_binary_ota_server_root_ca_pem_start");
#endif
#endif

}  // namespace

namespace service {

bool build_rcp_transport_request(const RcpUpdateRequest& request, hal_rcp_https_request_t* out) noexcept {
    if (out == nullptr || request.url[0] == '\0') {
        return false;
    }

    std::memset(out, 0, sizeof(*out));
    out->url = request.url.data();
    out->expected_sha256_hex = request.sha256[0] != '\0' ? request.sha256.data() : nullptr;
    out->expected_version = request.target_version[0] != '\0' ? request.target_version.data() : nullptr;
    out->timeout_ms = 30000U;
    out->allow_plain_http = (CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING != 0);

#if CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE
    out->tls_trust_mode = HAL_RCP_TLS_TRUST_CERT_BUNDLE;
#elif CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA
    out->tls_trust_mode = HAL_RCP_TLS_TRUST_PINNED_CA;
#ifdef ESP_PLATFORM
    out->trusted_root_ca_pem = ota_server_root_ca_pem_start;
#endif
#else
    out->tls_trust_mode = HAL_RCP_TLS_TRUST_NONE;
#endif

    return true;
}

}  // namespace service
