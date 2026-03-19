/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "web_routes.hpp"

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef void* httpd_handle_t;

typedef int (*httpd_uri_match_fn_t)(const char* reference_uri, const char* uri_to_match, std::size_t match_upto);

typedef struct {
    httpd_uri_match_fn_t uri_match_fn;
    int max_uri_handlers;
    int stack_size;
} httpd_config_t;

static int stub_uri_match_wildcard(const char* reference_uri, const char* uri_to_match, std::size_t match_upto) {
    (void)reference_uri;
    (void)uri_to_match;
    (void)match_upto;
    return 0;
}

httpd_uri_match_fn_t httpd_uri_match_wildcard = &stub_uri_match_wildcard;

static httpd_config_t make_default_httpd_config() {
    httpd_config_t cfg{};
    cfg.uri_match_fn = nullptr;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 4096;
    return cfg;
}

#define HTTPD_DEFAULT_CONFIG() make_default_httpd_config()

namespace {

int g_httpd_start_calls = 0;
int g_httpd_stop_calls = 0;
esp_err_t g_httpd_start_status = ESP_OK;
httpd_handle_t g_last_stopped_handle = nullptr;
int g_last_max_uri_handlers = 0;
httpd_uri_match_fn_t g_last_uri_match_fn = nullptr;

int g_register_routes_calls = 0;
bool g_register_routes_ok = true;
void* g_last_register_handle = nullptr;
web_ui::WebRouteContext* g_last_register_context = nullptr;

void reset_counters() {
    g_httpd_start_calls = 0;
    g_httpd_stop_calls = 0;
    g_httpd_start_status = ESP_OK;
    g_last_stopped_handle = nullptr;
    g_last_max_uri_handlers = 0;
    g_last_uri_match_fn = nullptr;
    g_register_routes_calls = 0;
    g_register_routes_ok = true;
    g_last_register_handle = nullptr;
    g_last_register_context = nullptr;
}

}  // namespace

extern "C" esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config) {
    ++g_httpd_start_calls;
    if (config != nullptr) {
        g_last_max_uri_handlers = config->max_uri_handlers;
        g_last_uri_match_fn = config->uri_match_fn;
    }

    if (g_httpd_start_status != ESP_OK) {
        return g_httpd_start_status;
    }

    if (handle != nullptr) {
        *handle = reinterpret_cast<void*>(0xBEEF);
    }
    return ESP_OK;
}

extern "C" esp_err_t httpd_stop(httpd_handle_t handle) {
    ++g_httpd_stop_calls;
    g_last_stopped_handle = handle;
    return ESP_OK;
}

namespace web_ui {

bool register_web_routes(void* server_handle, WebRouteContext* context) noexcept {
    ++g_register_routes_calls;
    g_last_register_handle = server_handle;
    g_last_register_context = context;
    return g_register_routes_ok;
}

}  // namespace web_ui

#include "../../components/web_ui/web_server.cpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    reset_counters();
    web_ui::WebServer server(runtime);
    assert(server.start());
    assert(server.started());
    assert(g_httpd_start_calls == 1);
    assert(g_register_routes_calls == 1);
    assert(g_last_max_uri_handlers == 24);
    assert(g_last_uri_match_fn == httpd_uri_match_wildcard);
    assert(g_last_register_handle == reinterpret_cast<void*>(0xBEEF));
    assert(g_last_register_context != nullptr);

    assert(server.start());
    assert(server.started());
    assert(g_httpd_start_calls == 1);
    assert(g_register_routes_calls == 1);

    server.stop();
    assert(!server.started());
    assert(g_httpd_stop_calls == 1);
    assert(g_last_stopped_handle == reinterpret_cast<void*>(0xBEEF));

    reset_counters();
    g_httpd_start_status = ESP_FAIL;
    web_ui::WebServer server_start_fail(runtime);
    assert(!server_start_fail.start());
    assert(!server_start_fail.started());
    assert(g_httpd_start_calls == 1);
    assert(g_register_routes_calls == 0);
    assert(g_httpd_stop_calls == 0);

    reset_counters();
    g_register_routes_ok = false;
    web_ui::WebServer server_routes_fail(runtime);
    assert(!server_routes_fail.start());
    assert(!server_routes_fail.started());
    assert(g_httpd_start_calls == 1);
    assert(g_register_routes_calls == 1);
    assert(g_httpd_stop_calls == 1);
    assert(g_last_stopped_handle == reinterpret_cast<void*>(0xBEEF));

    return 0;
}
