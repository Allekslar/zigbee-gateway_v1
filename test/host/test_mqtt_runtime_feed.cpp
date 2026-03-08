/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "mqtt_bridge.hpp"
#include "service_runtime.hpp"

namespace {

bool has_topic(const mqtt_bridge::MqttPublishedMessage* messages, std::size_t count, const char* topic) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(messages[i].topic, topic) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    mqtt_bridge::MqttBridge bridge;
    bridge.attach_runtime(&runtime);
    assert(bridge.start());

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x2201U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);

    assert(bridge.sync_runtime_snapshot() == 3U);
    mqtt_bridge::MqttPublishedMessage out[mqtt_bridge::kMaxMqttPublicationsPerSync]{};
    std::size_t drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 3U);
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/availability"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/state"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/telemetry"));

    assert(bridge.sync_runtime_snapshot() == 0U);

    core::CoreEvent left{};
    left.type = core::CoreEventType::kDeviceLeft;
    left.device_short_addr = 0x2201U;
    assert(runtime.post_event(left));
    assert(runtime.process_pending() == 1U);

    assert(bridge.sync_runtime_snapshot() == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/devices/8705/availability") == 0);
    assert(std::strcmp(out[0].payload, "offline") == 0);

    bridge.stop();
    return 0;
}
