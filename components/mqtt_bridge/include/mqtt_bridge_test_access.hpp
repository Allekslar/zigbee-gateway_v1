/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(MQTT_BRIDGE_TEST_HOOKS)
#error "mqtt_bridge_test_access.hpp is test-only and requires MQTT_BRIDGE_TEST_HOOKS"
#endif

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
};

}  // namespace mqtt_bridge
