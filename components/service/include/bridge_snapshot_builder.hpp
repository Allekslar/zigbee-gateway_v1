/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "core_registry.hpp"
#include "service_runtime_api.hpp"

namespace service {

class BridgeSnapshotBuilder {
public:
    explicit BridgeSnapshotBuilder(core::CoreRegistry& registry) noexcept : registry_(&registry) {}

    bool build_mqtt_snapshot(MqttBridgeSnapshot* out) const noexcept;
    bool build_matter_snapshot(MatterBridgeSnapshot* out) const noexcept;

private:
    core::CoreRegistry* registry_{nullptr};
};

}  // namespace service
