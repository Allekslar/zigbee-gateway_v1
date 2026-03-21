/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

namespace web_ui {

bool register_web_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr || context->runtime == nullptr ||
        context->next_correlation_id == nullptr) {
        return false;
    }

    if (!register_static_routes(server_handle, context)) {
        return false;
    }

    if (!register_device_routes(server_handle, context)) {
        return false;
    }

    if (!register_network_routes(server_handle, context)) {
        return false;
    }

    if (!register_config_routes(server_handle, context)) {
        return false;
    }

    if (!register_ota_routes(server_handle, context)) {
        return false;
    }

    if (!register_rcp_routes(server_handle, context)) {
        return false;
    }

    return true;
}

}  // namespace web_ui
