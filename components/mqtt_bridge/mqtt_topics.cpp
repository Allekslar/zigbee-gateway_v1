/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mqtt_topics.hpp"

#include <cstdio>

namespace mqtt_bridge {
namespace {

constexpr const char* kTopicRoot = "zigbee-gateway";

bool build_device_topic(const uint16_t short_addr,
                        const char* suffix,
                        char* out,
                        const std::size_t out_size) noexcept {
    if (out == nullptr || out_size == 0U || suffix == nullptr) {
        return false;
    }
    const int written = std::snprintf(out,
                                      out_size,
                                      "%s/devices/%u/%s",
                                      kTopicRoot,
                                      static_cast<unsigned>(short_addr),
                                      suffix);
    return written > 0 && static_cast<std::size_t>(written) < out_size;
}

}  // namespace

const char* topic_devices() noexcept {
    return "zigbee-gateway/devices";
}

const char* topic_state() noexcept {
    return "zigbee-gateway/state";
}

bool topic_device_state(const uint16_t short_addr, char* out, const std::size_t out_size) noexcept {
    return build_device_topic(short_addr, "state", out, out_size);
}

bool topic_device_telemetry(const uint16_t short_addr, char* out, const std::size_t out_size) noexcept {
    return build_device_topic(short_addr, "telemetry", out, out_size);
}

bool topic_device_availability(const uint16_t short_addr, char* out, const std::size_t out_size) noexcept {
    return build_device_topic(short_addr, "availability", out, out_size);
}

bool topic_device_config(const uint16_t short_addr, char* out, const std::size_t out_size) noexcept {
    return build_device_topic(short_addr, "config", out, out_size);
}

}  // namespace mqtt_bridge
