/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "core_events.hpp"

namespace mqtt_bridge {

struct MqttMessage {
    const char* topic{nullptr};
    char payload[64]{};
};

MqttMessage serialize_event(const core::CoreEvent& event) noexcept;

}  // namespace mqtt_bridge
