/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "bridge_snapshot_builder.hpp"
#include "devices_api_snapshot_builder.hpp"
#include "runtime_lock.hpp"
#include "service_runtime_api.hpp"

namespace core {
class CoreRegistry;
class CoreState;
}  // namespace core

namespace service {

class ReadModelCoordinator {
public:
    struct NetworkPublishInput {
        uint32_t revision{0};
        bool connected{false};
        uint32_t refresh_requests{0};
        uint32_t current_backoff_ms{0};
        NetworkApiSnapshot::MqttStatusSnapshot mqtt{};
    };

    struct ConfigPublishInput {
        uint32_t revision{0};
        uint8_t last_command_status{0};
        uint32_t command_timeout_ms{5000};
        uint8_t max_command_retries{1};
        uint32_t autoconnect_failures{0};
    };

    explicit ReadModelCoordinator(core::CoreRegistry& registry) noexcept;

    bool build_devices_api_snapshot(
        const core::CoreState& state,
        const DevicesRuntimeSnapshot& runtime_snapshot,
        DevicesApiSnapshot* out) const noexcept;
    bool build_mqtt_bridge_snapshot(MqttBridgeSnapshot* out) const noexcept;
    bool build_matter_bridge_snapshot(MatterBridgeSnapshot* out) const noexcept;

    void publish_network_snapshot(const NetworkPublishInput& input) noexcept;
    void publish_config_snapshot(const ConfigPublishInput& input) noexcept;
    void refresh_bridge_snapshots() noexcept;
    bool build_network_api_snapshot(NetworkApiSnapshot* out) const noexcept;
    bool build_config_api_snapshot(ConfigApiSnapshot* out) const noexcept;

private:
    struct NetworkApiSnapshotStorage {
        std::atomic<uint32_t> seq{0};
        std::atomic<uint32_t> revision{0};
        std::atomic<bool> connected{false};
        std::atomic<uint32_t> refresh_requests{0};
        std::atomic<uint32_t> current_backoff_ms{0};
        std::atomic<bool> mqtt_enabled{false};
        std::atomic<bool> mqtt_connected{false};
        std::atomic<uint32_t> mqtt_last_connect_error{0};
        std::array<char, NetworkApiSnapshot::MqttStatusSnapshot::kBrokerEndpointSummaryMaxLen>
            mqtt_broker_endpoint_summary{};
    };

    struct ConfigApiSnapshotStorage {
        std::atomic<uint32_t> seq{0};
        std::atomic<uint32_t> revision{0};
        std::atomic<uint32_t> last_command_status{0};
        std::atomic<uint32_t> command_timeout_ms{5000};
        std::atomic<uint32_t> max_command_retries{1};
        std::atomic<uint32_t> autoconnect_failures{0};
    };

    BridgeSnapshotBuilder bridge_snapshot_builder_;
    DevicesApiSnapshotBuilder devices_api_snapshot_builder_{};
    NetworkApiSnapshotStorage network_api_snapshot_{};
    ConfigApiSnapshotStorage config_api_snapshot_{};
    mutable RuntimeLock bridge_snapshot_lock_{};
    MqttBridgeSnapshot mqtt_bridge_snapshot_{};
    MatterBridgeSnapshot matter_bridge_snapshot_{};
    bool mqtt_bridge_snapshot_ready_{false};
    bool matter_bridge_snapshot_ready_{false};
};

}  // namespace service
