/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "read_model_coordinator.hpp"

#include <cstring>

namespace service {

ReadModelCoordinator::ReadModelCoordinator(core::CoreRegistry& registry) noexcept
    : bridge_snapshot_builder_(registry) {}

bool ReadModelCoordinator::build_devices_api_snapshot(
    const core::CoreState& state,
    const DevicesRuntimeSnapshot& runtime_snapshot,
    DevicesApiSnapshot* out) const noexcept {
    return devices_api_snapshot_builder_.build(state, runtime_snapshot, out);
}

bool ReadModelCoordinator::build_mqtt_bridge_snapshot(MqttBridgeSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    RuntimeLockGuard guard(bridge_snapshot_lock_);
    if (!mqtt_bridge_snapshot_ready_) {
        return false;
    }
    *out = mqtt_bridge_snapshot_;
    return true;
}

bool ReadModelCoordinator::build_matter_bridge_snapshot(MatterBridgeSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    RuntimeLockGuard guard(bridge_snapshot_lock_);
    if (!matter_bridge_snapshot_ready_) {
        return false;
    }
    *out = matter_bridge_snapshot_;
    return true;
}

void ReadModelCoordinator::publish_network_snapshot(const NetworkPublishInput& input) noexcept {
    const uint32_t start_seq = network_api_snapshot_.seq.load(std::memory_order_relaxed);
    network_api_snapshot_.seq.store(start_seq + 1U, std::memory_order_release);
    network_api_snapshot_.revision.store(input.revision, std::memory_order_relaxed);
    network_api_snapshot_.connected.store(input.connected, std::memory_order_relaxed);
    network_api_snapshot_.refresh_requests.store(input.refresh_requests, std::memory_order_relaxed);
    network_api_snapshot_.current_backoff_ms.store(input.current_backoff_ms, std::memory_order_relaxed);
    network_api_snapshot_.mqtt_enabled.store(input.mqtt.enabled, std::memory_order_relaxed);
    network_api_snapshot_.mqtt_connected.store(input.mqtt.connected, std::memory_order_relaxed);
    network_api_snapshot_.mqtt_last_connect_error.store(
        static_cast<uint32_t>(input.mqtt.last_connect_error),
        std::memory_order_relaxed);
    std::memcpy(
        network_api_snapshot_.mqtt_broker_endpoint_summary.data(),
        input.mqtt.broker_endpoint_summary.data(),
        network_api_snapshot_.mqtt_broker_endpoint_summary.size());
    network_api_snapshot_.seq.store(start_seq + 2U, std::memory_order_release);
}

void ReadModelCoordinator::publish_config_snapshot(const ConfigPublishInput& input) noexcept {
    const uint32_t start_seq = config_api_snapshot_.seq.load(std::memory_order_relaxed);
    config_api_snapshot_.seq.store(start_seq + 1U, std::memory_order_release);
    config_api_snapshot_.revision.store(input.revision, std::memory_order_relaxed);
    config_api_snapshot_.last_command_status.store(input.last_command_status, std::memory_order_relaxed);
    config_api_snapshot_.command_timeout_ms.store(input.command_timeout_ms, std::memory_order_relaxed);
    config_api_snapshot_.max_command_retries.store(input.max_command_retries, std::memory_order_relaxed);
    config_api_snapshot_.autoconnect_failures.store(input.autoconnect_failures, std::memory_order_relaxed);
    config_api_snapshot_.seq.store(start_seq + 2U, std::memory_order_release);
}

void ReadModelCoordinator::refresh_bridge_snapshots() noexcept {
    MqttBridgeSnapshot mqtt_snapshot{};
    const bool mqtt_ok = bridge_snapshot_builder_.build_mqtt_snapshot(&mqtt_snapshot);

    MatterBridgeSnapshot matter_snapshot{};
    const bool matter_ok = bridge_snapshot_builder_.build_matter_snapshot(&matter_snapshot);

    RuntimeLockGuard guard(bridge_snapshot_lock_);
    if (mqtt_ok) {
        mqtt_bridge_snapshot_ = mqtt_snapshot;
        mqtt_bridge_snapshot_ready_ = true;
    }
    if (matter_ok) {
        matter_bridge_snapshot_ = matter_snapshot;
        matter_bridge_snapshot_ready_ = true;
    }
}

bool ReadModelCoordinator::build_network_api_snapshot(NetworkApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    for (;;) {
        const uint32_t start_seq = network_api_snapshot_.seq.load(std::memory_order_acquire);
        if ((start_seq & 1U) != 0U) {
            continue;
        }

        NetworkApiSnapshot snapshot{};
        snapshot.revision = network_api_snapshot_.revision.load(std::memory_order_relaxed);
        snapshot.connected = network_api_snapshot_.connected.load(std::memory_order_relaxed);
        snapshot.refresh_requests = network_api_snapshot_.refresh_requests.load(std::memory_order_relaxed);
        snapshot.current_backoff_ms = network_api_snapshot_.current_backoff_ms.load(std::memory_order_relaxed);
        snapshot.mqtt.enabled = network_api_snapshot_.mqtt_enabled.load(std::memory_order_relaxed);
        snapshot.mqtt.connected = network_api_snapshot_.mqtt_connected.load(std::memory_order_relaxed);
        snapshot.mqtt.last_connect_error = static_cast<NetworkApiSnapshot::MqttConnectionError>(
            network_api_snapshot_.mqtt_last_connect_error.load(std::memory_order_relaxed));
        std::memcpy(
            snapshot.mqtt.broker_endpoint_summary.data(),
            network_api_snapshot_.mqtt_broker_endpoint_summary.data(),
            snapshot.mqtt.broker_endpoint_summary.size());

        const uint32_t end_seq = network_api_snapshot_.seq.load(std::memory_order_acquire);
        if (start_seq == end_seq) {
            *out = snapshot;
            return true;
        }
    }
}

bool ReadModelCoordinator::build_config_api_snapshot(ConfigApiSnapshot* out) const noexcept {
    if (out == nullptr) {
        return false;
    }

    for (;;) {
        const uint32_t start_seq = config_api_snapshot_.seq.load(std::memory_order_acquire);
        if ((start_seq & 1U) != 0U) {
            continue;
        }

        ConfigApiSnapshot snapshot{};
        snapshot.revision = config_api_snapshot_.revision.load(std::memory_order_relaxed);
        snapshot.last_command_status = static_cast<uint8_t>(
            config_api_snapshot_.last_command_status.load(std::memory_order_relaxed));
        snapshot.command_timeout_ms = config_api_snapshot_.command_timeout_ms.load(std::memory_order_relaxed);
        snapshot.max_command_retries = static_cast<uint8_t>(
            config_api_snapshot_.max_command_retries.load(std::memory_order_relaxed));
        snapshot.autoconnect_failures = config_api_snapshot_.autoconnect_failures.load(std::memory_order_relaxed);

        const uint32_t end_seq = config_api_snapshot_.seq.load(std::memory_order_acquire);
        if (start_seq == end_seq) {
            *out = snapshot;
            return true;
        }
    }
}

}  // namespace service
