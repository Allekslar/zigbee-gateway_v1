/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(MQTT_BRIDGE_TEST_HOOKS)
#error "mqtt_bridge_test_access.hpp is test-only and requires MQTT_BRIDGE_TEST_HOOKS"
#endif

#include <cstdio>

#include "mqtt_bridge.hpp"

namespace mqtt_bridge {

class MqttBridgeTestAccess {
public:
    static void handle_transport_connected(MqttBridge& bridge) noexcept {
        bridge.handle_transport_connected();
    }

    static void handle_transport_disconnected(MqttBridge& bridge) noexcept {
        bridge.handle_transport_disconnected();
    }

    static void handle_transport_error(
        MqttBridge& bridge,
        const service::NetworkApiSnapshot::MqttConnectionError error) noexcept {
        bridge.handle_transport_error(error);
    }

    static void handle_transport_subscribe_failure(MqttBridge& bridge) noexcept {
        bridge.handle_transport_subscribe_failure();
    }

    // Test-only seam: sync_snapshot()/sync_device_state() gate v1
    // publishing on a resolved gateway_id_hex_, normally populated from
    // ServiceRuntimeApi::gateway_id() via ensure_gateway_id_hex() (called
    // from attach_runtime()/handle_*_v1()). Tests that exercise
    // sync_snapshot() directly, without a ServiceRuntime to resolve a real
    // gateway_id from, use this to set it directly instead.
    static void set_gateway_id_hex_for_test(MqttBridge& bridge, const char* gateway_id_hex) noexcept {
        std::snprintf(bridge.gateway_id_hex_, sizeof(bridge.gateway_id_hex_), "%s", gateway_id_hex);
        bridge.gateway_id_hex_ready_ = true;
    }
};

}  // namespace mqtt_bridge
