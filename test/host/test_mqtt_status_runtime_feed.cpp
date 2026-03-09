/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "mqtt_bridge.hpp"
#include "mqtt_bridge_test_access.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    mqtt_bridge::MqttBridge bridge;
    bridge.attach_runtime(&runtime);
    assert(bridge.start());
    assert(runtime.process_pending() == 0U);

    service::NetworkApiSnapshot network{};
    assert(runtime.build_network_api_snapshot(&network));
    assert(network.mqtt.enabled);
    assert(network.mqtt.connected);
    assert(network.mqtt.last_connect_error == service::NetworkApiSnapshot::MqttConnectionError::kNone);

    mqtt_bridge::MqttBridgeTestAccess::handle_transport_disconnected(bridge);
    assert(runtime.process_pending() == 0U);
    assert(runtime.build_network_api_snapshot(&network));
    assert(network.mqtt.enabled);
    assert(!network.mqtt.connected);
    assert(network.mqtt.last_connect_error == service::NetworkApiSnapshot::MqttConnectionError::kNone);

    mqtt_bridge::MqttBridgeTestAccess::handle_transport_error(
        bridge,
        service::NetworkApiSnapshot::MqttConnectionError::kStartFailed);
    assert(runtime.process_pending() == 0U);
    assert(runtime.build_network_api_snapshot(&network));
    assert(network.mqtt.enabled);
    assert(!network.mqtt.connected);
    assert(network.mqtt.last_connect_error == service::NetworkApiSnapshot::MqttConnectionError::kStartFailed);

    mqtt_bridge::MqttBridgeTestAccess::handle_transport_connected(bridge);
    assert(runtime.process_pending() == 0U);
    assert(runtime.build_network_api_snapshot(&network));
    assert(network.mqtt.enabled);
    assert(network.mqtt.connected);
    assert(network.mqtt.last_connect_error == service::NetworkApiSnapshot::MqttConnectionError::kNone);

    mqtt_bridge::MqttBridgeTestAccess::handle_transport_subscribe_failure(bridge);
    assert(runtime.process_pending() == 0U);
    assert(runtime.build_network_api_snapshot(&network));
    assert(network.mqtt.enabled);
    assert(!network.mqtt.connected);
    assert(network.mqtt.last_connect_error == service::NetworkApiSnapshot::MqttConnectionError::kSubscribeFailed);

    bridge.stop();
    return 0;
}
