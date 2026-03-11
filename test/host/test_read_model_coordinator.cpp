/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "core_registry.hpp"
#include "read_model_coordinator.hpp"

int main() {
    core::CoreRegistry registry{};
    service::ReadModelCoordinator coordinator(registry);

    service::ReadModelCoordinator::NetworkPublishInput network_input{};
    network_input.refresh_requests = 7U;
    network_input.current_backoff_ms = 1500U;
    network_input.mqtt.enabled = true;
    network_input.mqtt.connected = false;
    network_input.mqtt.last_connect_error = service::NetworkApiSnapshot::MqttConnectionError::kStartFailed;
    std::strncpy(
        network_input.mqtt.broker_endpoint_summary.data(),
        "mqtt://broker.local:1883",
        network_input.mqtt.broker_endpoint_summary.size() - 1U);

    service::ReadModelCoordinator::ConfigPublishInput config_input{};
    config_input.command_timeout_ms = 8000U;
    config_input.max_command_retries = 3U;
    config_input.autoconnect_failures = 2U;

    service::ReadModelCoordinator::CorePublishInput core_input{};
    core_input.revision = 11U;
    core_input.network_connected = true;
    core_input.last_command_status = 5U;

    coordinator.on_runtime_stats_changed(network_input);
    coordinator.on_config_changed(config_input);
    coordinator.on_core_state_published(core_input);

    service::NetworkApiSnapshot network_snapshot{};
    assert(coordinator.build_network_api_snapshot(&network_snapshot));
    assert(network_snapshot.revision == 11U);
    assert(network_snapshot.connected);
    assert(network_snapshot.refresh_requests == 7U);
    assert(network_snapshot.current_backoff_ms == 1500U);
    assert(network_snapshot.mqtt.enabled);
    assert(!network_snapshot.mqtt.connected);
    assert(network_snapshot.mqtt.last_connect_error ==
           service::NetworkApiSnapshot::MqttConnectionError::kStartFailed);

    service::ConfigApiSnapshot config_snapshot{};
    assert(coordinator.build_config_api_snapshot(&config_snapshot));
    assert(config_snapshot.revision == 11U);
    assert(config_snapshot.last_command_status == 5U);
    assert(config_snapshot.command_timeout_ms == 8000U);
    assert(config_snapshot.max_command_retries == 3U);
    assert(config_snapshot.autoconnect_failures == 2U);

    service::NetworkApiSnapshot::MqttStatusSnapshot mqtt_connected{};
    mqtt_connected.enabled = true;
    mqtt_connected.connected = true;
    mqtt_connected.last_connect_error = service::NetworkApiSnapshot::MqttConnectionError::kNone;
    coordinator.on_mqtt_status_changed(mqtt_connected);

    assert(coordinator.build_network_api_snapshot(&network_snapshot));
    assert(network_snapshot.mqtt.connected);
    assert(network_snapshot.mqtt.last_connect_error ==
           service::NetworkApiSnapshot::MqttConnectionError::kNone);

    return 0;
}
