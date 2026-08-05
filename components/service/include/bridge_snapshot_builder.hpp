/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "core_registry.hpp"
#include "matter_endpoint_registry.hpp"
#include "service_runtime_api.hpp"

namespace service {

class BridgeSnapshotBuilder {
public:
    BridgeSnapshotBuilder(core::CoreRegistry& registry, const MatterEndpointRegistry& matter_endpoint_registry) noexcept
        : registry_(&registry), matter_endpoint_registry_(&matter_endpoint_registry) {}

    bool build_mqtt_snapshot(MqttBridgeSnapshot* out) const noexcept;
    bool build_matter_snapshot(MatterBridgeSnapshot* out) const noexcept;

private:
    core::CoreRegistry* registry_{nullptr};
    const MatterEndpointRegistry* matter_endpoint_registry_{nullptr};
};

}  // namespace service
