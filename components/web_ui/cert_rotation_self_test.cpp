/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "cert_rotation_self_test.hpp"

#ifdef ESP_PLATFORM
#include <cstdio>
#include "esp_http_client.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "log_tags.h"
#endif

namespace web_ui {

#ifdef ESP_PLATFORM

namespace {

constexpr const char* kTag = LOG_TAG_WEB_SERVER;

// A dedicated non-production port -- production's own listener
// (web_server.cpp's start_production_https()) already owns 443 and
// keeps running throughout this self-test; this temporary listener must
// never collide with it.
constexpr uint16_t kCertRotationSelfTestPort = 8443U;

// Deliberately small -- this listener serves exactly one trivial route
// to exactly one loopback client for a few hundred milliseconds at
// most, nothing like production's own 20480-byte/56-handler budget.
// Matches esp_http_server's own library default (HTTPD_DEFAULT_CONFIG's
// stack_size), not a value picked to be "safe" without a real basis.
constexpr size_t kSelfTestStackSize = 4096U;
constexpr uint16_t kSelfTestMaxUriHandlers = 1U;
constexpr uint16_t kSelfTestMaxOpenSockets = 1U;

// Long enough for esp_http_server to flush the route handler's own HTTP
// response over the socket before the device restarts; short enough
// that an installer isn't left wondering whether the request was lost.
constexpr uint64_t kCertRotationRebootDelayUs = 2000000ULL;  // 2s

esp_err_t self_test_health_handler(httpd_req_t* req) {
    (void)httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

void reboot_timer_callback(void* /*arg*/) {
    esp_restart();
}

}  // namespace

bool cert_rotation_bounded_self_test(
    const uint8_t* cert_pem, uint32_t cert_pem_len, const uint8_t* key_pem, uint32_t key_pem_len,
    const uint8_t* ca_pem, uint32_t ca_pem_len, const char* expected_dns_san) noexcept {
    if (cert_pem == nullptr || key_pem == nullptr || ca_pem == nullptr || expected_dns_san == nullptr) {
        return false;
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.stack_size = kSelfTestStackSize;
    config.httpd.max_uri_handlers = kSelfTestMaxUriHandlers;
    config.httpd.max_open_sockets = kSelfTestMaxOpenSockets;
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    config.port_secure = kCertRotationSelfTestPort;
    config.servercert = cert_pem;
    config.servercert_len = cert_pem_len;
    config.prvtkey_pem = key_pem;
    config.prvtkey_len = key_pem_len;

    httpd_handle_t self_test_handle = nullptr;
    if (httpd_ssl_start(&self_test_handle, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Certificate rotation self-test: temporary listener failed to start");
        return false;
    }

    httpd_uri_t health_uri{};
    health_uri.uri = "/";
    health_uri.method = HTTP_GET;
    health_uri.handler = self_test_health_handler;
    health_uri.user_ctx = nullptr;
    if (httpd_register_uri_handler(self_test_handle, &health_uri) != ESP_OK) {
        ESP_LOGE(kTag, "Certificate rotation self-test: health route registration failed");
        (void)httpd_ssl_stop(self_test_handle);
        return false;
    }

    // One real HTTPS request over loopback -- the connection target
    // (127.0.0.1) is deliberately decoupled from the identity check
    // (`common_name` = expected_dns_san, this gateway's own real
    // production DNS SAN) via esp_http_client_config_t's own documented
    // mechanism for exactly this split. Trusts `ca_pem` (the same
    // product CA production validates against), not the candidate's own
    // certificate -- a self-signed or wrongly-issued candidate must fail
    // this the same way a real remote client's trust store would reject
    // it.
    char url[48]{};
    (void)std::snprintf(url, sizeof(url), "https://127.0.0.1:%u/", static_cast<unsigned>(kCertRotationSelfTestPort));

    esp_http_client_config_t client_config{};
    client_config.url = url;
    client_config.common_name = expected_dns_san;
    client_config.cert_pem = reinterpret_cast<const char*>(ca_pem);
    client_config.cert_len = ca_pem_len;
    client_config.timeout_ms = 5000;
    client_config.buffer_size = 512;
    client_config.buffer_size_tx = 512;

    bool handshake_ok = false;
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client != nullptr) {
        const esp_err_t perform_result = esp_http_client_perform(client);
        if (perform_result == ESP_OK) {
            const int status = esp_http_client_get_status_code(client);
            handshake_ok = (status == 200);
            if (!handshake_ok) {
                ESP_LOGE(kTag, "Certificate rotation self-test: unexpected HTTP status %d", status);
            }
        } else {
            ESP_LOGE(kTag, "Certificate rotation self-test: request failed (esp_err=0x%x)", (unsigned)perform_result);
        }
        (void)esp_http_client_cleanup(client);
    } else {
        ESP_LOGE(kTag, "Certificate rotation self-test: esp_http_client_init failed");
    }

    // Bounded: the temporary listener is torn down here, on every path
    // that reaches this point, regardless of handshake_ok.
    (void)httpd_ssl_stop(self_test_handle);
    return handshake_ok;
}

void schedule_cert_rotation_reboot() noexcept {
    // `static`: esp_timer_create() requires the handle to remain valid
    // for the timer's own lifetime, and a one-shot timer that is about
    // to reboot the device has no meaningful "stop and reuse" call site
    // -- matches the button-poll timer's own `static` handle precedent
    // in web_server.cpp.
    static esp_timer_handle_t s_reboot_timer = nullptr;
    const esp_timer_create_args_t reboot_timer_args = {
        &reboot_timer_callback, nullptr, ESP_TIMER_TASK, "cert_rotation_reboot", false};
    if (esp_timer_create(&reboot_timer_args, &s_reboot_timer) != ESP_OK ||
        esp_timer_start_once(s_reboot_timer, kCertRotationRebootDelayUs) != ESP_OK) {
        ESP_LOGE(kTag, "Certificate rotation: reboot scheduling failed -- device will not restart into the newly "
                       "activated slot until the next unrelated reboot");
    }
}

#else

bool cert_rotation_bounded_self_test(
    const uint8_t* cert_pem, uint32_t cert_pem_len, const uint8_t* key_pem, uint32_t key_pem_len,
    const uint8_t* ca_pem, uint32_t ca_pem_len, const char* expected_dns_san) noexcept {
    // Host builds have no real esp_https_server/esp_http_client runtime
    // to exercise a genuine TLS handshake against -- fails closed, same
    // "no real crypto/network capability on host" boundary
    // hal_tls_certificate_validator.h's own host branch already
    // established for this codebase.
    (void)cert_pem;
    (void)cert_pem_len;
    (void)key_pem;
    (void)key_pem_len;
    (void)ca_pem;
    (void)ca_pem_len;
    (void)expected_dns_san;
    return false;
}

void schedule_cert_rotation_reboot() noexcept {
    // No-op on host: esp_restart()/esp_timer do not exist there, and
    // cert_rotation_bounded_self_test() above already always returns
    // false on host, so no real call site ever reaches this in a host
    // test today. Defined (not omitted) so the certificates/operations
    // route handler can call it unconditionally, matching this file's
    // own established "host branch exists and is inert" pattern.
}

#endif

}  // namespace web_ui
