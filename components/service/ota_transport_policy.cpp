/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "ota_transport_policy.hpp"

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

bool build_ota_transport_request(const OtaStartRequest& request, hal_ota_https_request_t* out) noexcept {
    if (out == nullptr || request.manifest.url[0] == '\0') {
        return false;
    }

    std::memset(out, 0, sizeof(*out));
    out->url = request.manifest.url.data();
    out->expected_version = request.manifest.version[0] != '\0' ? request.manifest.version.data() : nullptr;
    out->expected_project_name = request.manifest.project[0] != '\0' ? request.manifest.project.data() : nullptr;
    out->timeout_ms = 15000U;
    out->allow_plain_http = (CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING != 0);

#if CONFIG_ZGW_OTA_TLS_TRUST_CERT_BUNDLE
    out->tls_trust_mode = HAL_OTA_TLS_TRUST_CERT_BUNDLE;
#elif CONFIG_ZGW_OTA_TLS_TRUST_PINNED_CA
    out->tls_trust_mode = HAL_OTA_TLS_TRUST_PINNED_CA;
#ifdef ESP_PLATFORM
    out->trusted_root_ca_pem = ota_server_root_ca_pem_start;
#endif
#else
    out->tls_trust_mode = HAL_OTA_TLS_TRUST_NONE;
#endif

    return true;
}

}  // namespace service
