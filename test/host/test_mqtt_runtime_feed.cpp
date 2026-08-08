/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "device_id.hpp"
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

const mqtt_bridge::MqttPublishedMessage* find_topic(
    const mqtt_bridge::MqttPublishedMessage* messages, std::size_t count, const char* topic) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(messages[i].topic, topic) == 0) {
            return &messages[i];
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    mqtt_bridge::MqttBridge bridge;
    bridge.attach_runtime(&runtime);
    assert(bridge.start());

    core::DeviceId device_id{};
    assert(core::DeviceId::parse("00124b0001aa2201", 16, &device_id));

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_id = device_id;
    joined.device_short_addr = 0x2201U;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1U);

    // First sync after start(): the resolved-identity device gets 3 v1
    // publications (availability/state/telemetry) AND, once per boot, 3
    // legacy retained-empty-payload tombstones for the same device's
    // legacy short_addr topics (plan S4 MQTT #14).
    assert(bridge.sync_runtime_snapshot() == 6U);
    mqtt_bridge::MqttPublishedMessage out[mqtt_bridge::kMaxMqttPublicationsPerSync]{};
    std::size_t drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 6U);

    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/availability"));
    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/state"));
    assert(has_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/telemetry"));

    const mqtt_bridge::MqttPublishedMessage* v1_availability =
        find_topic(out, drained, "zigbee-gateway/v1/devices/00124b0001aa2201/availability");
    assert(v1_availability != nullptr);
    assert(std::strstr(v1_availability->payload, "\"schema_version\":1") != nullptr);
    assert(std::strstr(v1_availability->payload, "\"online\":true") != nullptr);

    // Legacy topics carry only the one-time empty tombstone -- never real
    // content -- after cutover.
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/availability"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/state"));
    assert(has_topic(out, drained, "zigbee-gateway/devices/8705/telemetry"));
    const mqtt_bridge::MqttPublishedMessage* legacy_availability =
        find_topic(out, drained, "zigbee-gateway/devices/8705/availability");
    assert(legacy_availability != nullptr);
    assert(legacy_availability->payload[0] == '\0');

    // Second sync: no change, and the one-time tombstone sweep never fires
    // again.
    assert(bridge.sync_runtime_snapshot() == 0U);

    core::CoreEvent left{};
    left.type = core::CoreEventType::kDeviceLeft;
    left.device_id = device_id;
    left.device_short_addr = 0x2201U;
    assert(runtime.post_event(left));
    assert(runtime.process_pending() == 1U);

    // Device gone: only the v1 availability:false publication -- no
    // legacy topic is ever touched again (plan S4 MQTT #15).
    assert(bridge.sync_runtime_snapshot() == 1U);
    drained = bridge.drain_publications(out, mqtt_bridge::kMaxMqttPublicationsPerSync);
    assert(drained == 1U);
    assert(std::strcmp(out[0].topic, "zigbee-gateway/v1/devices/00124b0001aa2201/availability") == 0);
    assert(std::strstr(out[0].payload, "\"online\":false") != nullptr);

    bridge.stop();
    return 0;
}
