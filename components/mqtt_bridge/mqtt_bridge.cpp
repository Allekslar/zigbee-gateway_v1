/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_bridge.hpp"

namespace mqtt_bridge {

bool MqttBridge::start() noexcept {
    publish_discovery();
    reset_sync_cache();
    started_ = true;
    return started_;
}

void MqttBridge::stop() noexcept {
    reset_sync_cache();
    started_ = false;
}

bool MqttBridge::started() const noexcept {
    return started_;
}

void MqttBridge::reset_sync_cache() noexcept {
    cached_device_count_ = 0;
    cache_initialized_ = false;
    pending_publication_count_ = 0;
}

}  // namespace mqtt_bridge
