/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "device_manager.hpp"
#include "service_runtime_api.hpp"

namespace core {
class CoreState;
}

namespace service {

class DevicesApiSnapshotBuilder {
public:
    bool build(
        const core::CoreState& state,
        const DeviceRuntimeSnapshot& runtime_snapshot,
        DevicesApiSnapshot* out) const noexcept;
};

}  // namespace service
