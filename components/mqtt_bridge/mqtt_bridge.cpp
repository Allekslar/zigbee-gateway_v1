/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_bridge.hpp"

namespace mqtt_bridge {

bool MqttBridge::start() noexcept {
    publish_discovery();
    started_ = true;
    return started_;
}

void MqttBridge::stop() noexcept {
    started_ = false;
}

bool MqttBridge::started() const noexcept {
    return started_;
}

}  // namespace mqtt_bridge
