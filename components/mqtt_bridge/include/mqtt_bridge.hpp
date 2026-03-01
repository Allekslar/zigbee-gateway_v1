/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace mqtt_bridge {

class MqttBridge {
public:
    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;

private:
    bool started_{false};
};

void publish_discovery() noexcept;
void sync_device_state(uint16_t short_addr, bool on) noexcept;

}  // namespace mqtt_bridge
