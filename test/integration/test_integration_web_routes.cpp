/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <atomic>

#include "web_routes.hpp"

namespace {

bool g_static_ok = true;
bool g_device_ok = true;
bool g_network_ok = true;
bool g_config_ok = true;

int g_call_index = 0;
int g_static_call_order = 0;
int g_device_call_order = 0;
int g_network_call_order = 0;
int g_config_call_order = 0;

void reset_expectations() {
    g_static_ok = true;
    g_device_ok = true;
    g_network_ok = true;
    g_config_ok = true;
    g_call_index = 0;
    g_static_call_order = 0;
    g_device_call_order = 0;
    g_network_call_order = 0;
    g_config_call_order = 0;
}

}  // namespace

namespace web_ui {

bool register_static_routes(void* server_handle, WebRouteContext* context) noexcept {
    (void)server_handle;
    (void)context;
    g_static_call_order = ++g_call_index;
    return g_static_ok;
}

bool register_device_routes(void* server_handle, WebRouteContext* context) noexcept {
    (void)server_handle;
    (void)context;
    g_device_call_order = ++g_call_index;
    return g_device_ok;
}

bool register_network_routes(void* server_handle, WebRouteContext* context) noexcept {
    (void)server_handle;
    (void)context;
    g_network_call_order = ++g_call_index;
    return g_network_ok;
}

bool register_config_routes(void* server_handle, WebRouteContext* context) noexcept {
    (void)server_handle;
    (void)context;
    g_config_call_order = ++g_call_index;
    return g_config_ok;
}

}  // namespace web_ui

#include "../../components/web_ui/web_routes.cpp"

int main() {
    std::atomic<uint32_t> next_correlation_id{1U};
    web_ui::WebRouteContext context{};
    context.registry = reinterpret_cast<core::CoreRegistry*>(0x1);
    context.runtime = reinterpret_cast<service::ServiceRuntime*>(0x1);
    context.next_correlation_id = &next_correlation_id;

    assert(!web_ui::register_web_routes(nullptr, &context));
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), nullptr));

    web_ui::WebRouteContext invalid_context = context;
    invalid_context.registry = nullptr;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &invalid_context));
    invalid_context = context;
    invalid_context.runtime = nullptr;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &invalid_context));
    invalid_context = context;
    invalid_context.next_correlation_id = nullptr;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &invalid_context));

    reset_expectations();
    g_static_ok = false;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &context));
    assert(g_static_call_order == 1);
    assert(g_device_call_order == 0);

    reset_expectations();
    g_device_ok = false;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &context));
    assert(g_static_call_order == 1);
    assert(g_device_call_order == 2);
    assert(g_network_call_order == 0);

    reset_expectations();
    g_network_ok = false;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &context));
    assert(g_network_call_order == 3);
    assert(g_config_call_order == 0);

    reset_expectations();
    g_config_ok = false;
    assert(!web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &context));
    assert(g_config_call_order == 4);

    reset_expectations();
    assert(web_ui::register_web_routes(reinterpret_cast<void*>(0x1), &context));
    assert(g_static_call_order == 1);
    assert(g_device_call_order == 2);
    assert(g_network_call_order == 3);
    assert(g_config_call_order == 4);

    return 0;
}
