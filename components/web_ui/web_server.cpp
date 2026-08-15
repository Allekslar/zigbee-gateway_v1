/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_server.hpp"

#ifdef ESP_PLATFORM
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "gateway_id.hpp"
#include "hal_tls_certificate_validator.h"
#include "provisioning_secrets.hpp"
#include "secure_storage_port.hpp"
#include "service_runtime_api.hpp"
#include "tls_provisioning_storage_port.hpp"
#endif

#include "log_tags.h"
#include "web_routes.hpp"

// Plan S6 "HTTPS and sessions" #7: "Replace production `httpd_start` with
// HTTPS server configuration. Development HTTP is gated by an explicit
// non-production profile."
//
// Adapter selection is CONFIG_ZGW_PRODUCTION_PROFILE (main/Kconfig.
// projbuild, introduced in S6-provisioning-credentials-completion.json for
// exactly this kind of dev/prod switch) -- never CONFIG_ZGW_PRODUCTION_BUILD,
// which is a build-time-only CMake variable that never becomes a compiled
// macro.
//
// Production reads the S5 "current" certificate/private-key slot
// (tls_provisioning_storage_port.hpp, plan S5 #13) and starts a real TLS
// listener via ESP-IDF's esp_https_server component
// (httpd_ssl_config_t/httpd_ssl_start(), confirmed against the real headers
// and httpd_ssl_start()'s real implementation inside
// espressif/idf:release-v5.5 before writing this file -- including
// confirming CONFIG_ESP_HTTPS_SERVER_ENABLE is not actually referenced
// anywhere in that component's own source, only httpd_ssl_start/_stop
// compiling unconditionally once esp_https_server is a REQUIRES
// dependency; sdkconfig.defaults.esp32c6 still sets it for
// menuconfig-visible documentation). If either the certificate or the
// private key is not SecureStorageStatus::kAvailable, the production
// listener refuses to start at all -- fails closed, never falls back to
// plain HTTP -- matching the plan's own closing text: "the production
// listener and mutation routes remain disabled until certificate trust...
// [is] healthy." Since no real certificate-issuance/rotation workflow
// exists yet (plan #10-#12, a separate, not-yet-implemented sub-slice;
// S5's own storage interfaces "generate... no real certificate, private
// key or provisioning secret"), a production build's HTTPS listener will
// always refuse to start today -- an expected, documented consequence of
// this sub-slice's own scope, not a bug.
//
// Plan #10/#11's own validation -- certificate chain-to-CA, exact DNS
// SAN (production mDNS host + ".local") and URI SAN
// ("urn:zgw:<gateway_id>"), expiry/not-yet-valid, and private-key/
// certificate pairing -- is delegated to app_hal's
// hal_tls_certificate_validator.h (hal_tls_validate_certificate() --
// a plain-C hal_* module, not a components/service port: it calls real
// mbedtls X.509/PK parsing that internally heap-allocates and must be
// explicitly freed, which check_arch_invariants.sh's INV-H002 correctly
// flagged when an earlier version of this validation lived under
// components/service instead), called below after all three
// (certificate/key/CA) are confirmed present. Missing CA, invalid
// issuer/SAN/expiry/key match, or an unreadable current slot all keep
// this listener from starting, matching #11's own text exactly.
//
// Data-format contract for whichever future sub-slice populates the
// "current" cert/key slot (plan #10-#12): stored bytes must be
// NUL-terminated PEM text, matching mbedtls_x509_crt_parse()'s own PEM
// convention ("buflen must include the terminating null byte"). This file
// passes the stored length through to servercert_len/prvtkey_len exactly
// as read -- it does not itself append or verify a NUL terminator.
//
// Development (CONFIG_ZGW_PRODUCTION_PROFILE unset): unchanged plain-HTTP
// behavior via httpd_start(), now explicitly gated behind "not
// production" rather than being the only code path -- satisfies the
// plan's "Development HTTP is gated by an explicit non-production
// profile" text.

namespace web_ui {

#ifdef ESP_PLATFORM
namespace {

constexpr const char* kTag = LOG_TAG_WEB_SERVER;

// Generous fixed bound for a single leaf certificate/private key in PEM
// form -- matches secure_storage_get_blob()'s host-mock ceiling
// coincidentally; the real ESP_PLATFORM NVS blob path has no such fixed
// limit of its own (bounded only by the NVS partition), this is purely
// this call site's own defensive stack-buffer size.
constexpr uint32_t kMaxCertOrKeyBytes = 4096U;

httpd_config_t build_base_httpd_config() noexcept {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Keep headroom for future API additions.
    config.max_uri_handlers = 24;
    // Keep the HTTPD socket ceiling modest so MQTT and other system paths
    // still retain headroom in the tiny ESP32-C6 socket budget.
    config.max_open_sockets = 4;
    config.backlog_conn = 8;
    // Devices/network snapshots grew after Phase 3 (OTA + Tuya identity
    // fields), so the 12KB HTTPD stack is no longer sufficient under real
    // UI polling. Keep the runtime behavior close to the rollback
    // candidate, but restore a safer stack budget to avoid mixing stack
    // faults with Zigbee regressions.
    config.stack_size = 20480;
    // Keep the HTTP socket lifecycle simple under mixed static-asset
    // fetches and API polling: prefer short-lived connections over long
    // keep-alive reuse so browser-held idle sockets do not consume most
    // of the tiny ESP32-C6 budget.
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 15;
    config.enable_so_linger = false;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 2;
    return config;
}

#if defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
bool start_production_https(const common::GatewayId& gateway_id, httpd_handle_t* out_handle) noexcept {
    // `static`, not stack-local: three kMaxCertOrKeyBytes (4096-byte)
    // buffers -- 12KB total -- as ordinary locals overflowed the calling
    // task's stack (the "main" task app_main() runs on, only ~4KB of its
    // own budget), a real "Guru Meditation Error: Stack protection
    // fault" observed on real ESP32-C6 hardware the first time this
    // function ever actually ran (every prior verification of this
    // function was compile-and-link only -- see this sub-slice's own
    // evidence file). `static` is additionally required for
    // *correctness*, not just stack safety: `config.servercert`/
    // `config.prvtkey_pem` below are raw pointers into these buffers,
    // and httpd_ssl_start()/esp_https_server's own internal handling of
    // them was not confirmed (via recon) to make a durable copy the way
    // mbedtls_x509_crt_parse() does -- these buffers must remain valid
    // for as long as the HTTPS listener itself runs, not just for this
    // function's own call. Safe as `static` here because
    // start_production_https() has exactly one call site
    // (WebServer::start(), itself called exactly once from app_main() on
    // the "main" task at boot).
    static uint8_t s_cert_bytes[kMaxCertOrKeyBytes];
    uint32_t cert_len = 0U;
    if (service::tls_identity_get_certificate(
            service::TlsCertificateSlot::kCurrent, s_cert_bytes, sizeof(s_cert_bytes), &cert_len) !=
        service::SecureStorageStatus::kAvailable) {
        ESP_LOGE(kTag, "Production HTTPS listener: current certificate unavailable -- refusing to start (fail closed)");
        return false;
    }

    static uint8_t s_key_bytes[kMaxCertOrKeyBytes];
    uint32_t key_len = 0U;
    if (service::tls_identity_get_private_key(
            service::TlsCertificateSlot::kCurrent, s_key_bytes, sizeof(s_key_bytes), &key_len) !=
        service::SecureStorageStatus::kAvailable) {
        ESP_LOGE(kTag, "Production HTTPS listener: current private key unavailable -- refusing to start (fail closed)");
        return false;
    }

    static uint8_t s_ca_bytes[kMaxCertOrKeyBytes];
    uint32_t ca_len = 0U;
    if (service::tls_identity_get_product_ca(s_ca_bytes, sizeof(s_ca_bytes), &ca_len) !=
        service::SecureStorageStatus::kAvailable) {
        ESP_LOGE(kTag, "Production HTTPS listener: product CA unavailable -- refusing to start (fail closed)");
        return false;
    }

    // Plan #10/#11: chain-to-CA, exact DNS SAN (production mDNS host +
    // ".local" -- build_gateway_mdns_host() alone does not include the
    // suffix, hal_mdns.h's own mdns_hostname_set() appends it separately)
    // and URI SAN ("urn:zgw:<gateway_id>"), expiry, and private-key/
    // certificate pairing.
    char mdns_host[32]{};
    char dns_san[40]{};
    char uri_san[32]{};
    if (!service::build_gateway_mdns_host(gateway_id, mdns_host, sizeof(mdns_host)) ||
        std::snprintf(dns_san, sizeof(dns_san), "%s.local", mdns_host) <= 0 ||
        !service::build_gateway_uri_san(gateway_id, uri_san, sizeof(uri_san))) {
        ESP_LOGE(kTag, "Production HTTPS listener: could not build expected SAN values -- refusing to start");
        return false;
    }

    const hal_tls_cert_validation_result_t validation = hal_tls_validate_certificate(
        s_cert_bytes, cert_len, s_key_bytes, key_len, s_ca_bytes, ca_len, dns_san, uri_san);
    if (validation != HAL_TLS_CERT_VALIDATION_VALID) {
        ESP_LOGE(
            kTag, "Production HTTPS listener: certificate validation failed (result=%u) -- refusing to start "
                  "(fail closed)",
            (unsigned)validation);
        return false;
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd = build_base_httpd_config();
    config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    config.servercert = s_cert_bytes;
    config.servercert_len = cert_len;
    config.prvtkey_pem = s_key_bytes;
    config.prvtkey_len = key_len;
    ESP_LOGI(
        kTag, "Starting production HTTPS server stack_size=%u max_uri_handlers=%u", config.httpd.stack_size,
        config.httpd.max_uri_handlers);

    httpd_handle_t handle = nullptr;
    if (httpd_ssl_start(&handle, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Production HTTPS listener failed to start");
        return false;
    }
    *out_handle = handle;
    return true;
}
#else
bool start_development_http(httpd_handle_t* out_handle) noexcept {
    httpd_config_t config = build_base_httpd_config();
    ESP_LOGI(
        kTag, "Starting development HTTP server (non-production profile) stack_size=%u max_uri_handlers=%u",
        config.stack_size, config.max_uri_handlers);

    httpd_handle_t handle = nullptr;
    if (httpd_start(&handle, &config) != ESP_OK) {
        return false;
    }
    *out_handle = handle;
    return true;
}
#endif  // CONFIG_ZGW_PRODUCTION_PROFILE

}  // namespace
#endif  // ESP_PLATFORM

WebServer::WebServer(service::ServiceRuntimeApi& runtime) noexcept
    : runtime_(&runtime) {
    route_context_.runtime = runtime_;
    route_context_.next_correlation_id = &next_correlation_id_;
    route_context_.sessions = &session_store_;
    route_context_.expected_origin = expected_origin_;
}

bool WebServer::start() noexcept {
    if (started_) {
        return true;
    }

    if (runtime_ == nullptr) {
        return false;
    }

#ifdef ESP_PLATFORM
    httpd_handle_t handle = nullptr;
#if defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
    using_https_ = true;
    if (!start_production_https(runtime_->gateway_id(), &handle)) {
        return false;
    }

    // Plan S6 "Authorization and physical presence" #15: this gateway's
    // own origin, built once here from the exact same production mDNS
    // host plan #9 derives -- "https://" always (production speaks HTTPS
    // only, plan #7's own branch above).
    char mdns_host[32]{};
    if (!service::build_gateway_mdns_host(runtime_->gateway_id(), mdns_host, sizeof(mdns_host)) ||
        std::snprintf(expected_origin_, sizeof(expected_origin_), "https://%s.local", mdns_host) <= 0) {
        (void)httpd_ssl_stop(handle);
        return false;
    }
#else
    using_https_ = false;
    if (!start_development_http(&handle)) {
        return false;
    }
#endif

    if (!register_web_routes(static_cast<void*>(handle), &route_context_)) {
        if (using_https_) {
            (void)httpd_ssl_stop(handle);
        } else {
            (void)httpd_stop(handle);
        }
        return false;
    }

#if defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
    // Plan #19: the first-ever production registration of the /api/v1
    // contract (plan S4 #28/#29's "S6 owns the first production
    // registration path") -- gated behind everything above: certificate/
    // trust validation (plan #7/#10/#11, start_production_https()) and
    // secure-storage readiness, with central authentication/authorization
    // (this function's own route_context_.sessions/expected_origin) wired
    // in via every route's register_authenticated_uri_handler_v1() call.
    // Development never calls this -- v1 sessions require the `Secure`
    // cookie attribute (plan #14), meaningless without HTTPS.
    if (!register_web_routes_v1(static_cast<void*>(handle), &route_context_)) {
        (void)httpd_ssl_stop(handle);
        return false;
    }
#endif

    server_handle_ = static_cast<void*>(handle);
    started_ = true;
    return started_;
#else
    return false;
#endif
}

void WebServer::stop() noexcept {
#ifdef ESP_PLATFORM
    if (server_handle_ != nullptr) {
        if (using_https_) {
            (void)httpd_ssl_stop(static_cast<httpd_handle_t>(server_handle_));
        } else {
            (void)httpd_stop(static_cast<httpd_handle_t>(server_handle_));
        }
        server_handle_ = nullptr;
    }
#endif

    started_ = false;
}

bool WebServer::started() const noexcept {
    return started_;
}

}  // namespace web_ui
