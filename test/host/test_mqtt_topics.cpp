/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "mqtt_topics.hpp"

int main() {
    using namespace mqtt_bridge;

    assert(std::strcmp(topic_devices(), "zigbee-gateway/devices") == 0);
    assert(std::strcmp(topic_state(), "zigbee-gateway/state") == 0);

    char topic[kTopicMaxLen] = {0};

    assert(topic_device_state(0x1234U, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/devices/4660/state") == 0);

    assert(topic_device_telemetry(0xABCDU, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/devices/43981/telemetry") == 0);

    assert(topic_device_availability(1U, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/devices/1/availability") == 0);

    assert(topic_device_config(65535U, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/devices/65535/config") == 0);
    assert(std::strcmp(topic_device_config_wildcard(), "zigbee-gateway/devices/+/config") == 0);

    assert(topic_device_power_set(4660U, topic, sizeof(topic)));
    assert(std::strcmp(topic, "zigbee-gateway/devices/4660/power/set") == 0);
    assert(std::strcmp(topic_device_power_set_wildcard(), "zigbee-gateway/devices/+/power/set") == 0);

    char tiny[8] = {0};
    assert(!topic_device_telemetry(0x1234U, tiny, sizeof(tiny)));
    assert(!topic_device_state(0x1234U, nullptr, sizeof(topic)));
    assert(!topic_device_state(0x1234U, topic, 0U));

    return 0;
}
