/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "mqtt_serializer.hpp"

int main() {
    using namespace mqtt_bridge;

    MqttSensorSnapshot full{};
    full.has_temperature = true;
    full.temperature_centi_c = 2150;
    full.occupancy = service::DeviceOccupancyState::kOccupied;
    full.contact_state = service::DeviceContactState::kOpen;
    full.contact_tamper = true;
    full.contact_battery_low = false;
    full.has_battery_percent = true;
    full.battery_percent = 74;
    full.has_battery_voltage = true;
    full.battery_voltage_mv = 3000;
    full.has_lqi = true;
    full.lqi = 201;
    full.has_rssi = true;
    full.rssi_dbm = -63;
    full.stale = false;
    full.timestamp_ms = 4242;

    char payload_a[kMqttPayloadMaxLen] = {0};
    char payload_b[kMqttPayloadMaxLen] = {0};
    std::size_t len_a = 0;
    std::size_t len_b = 0;

    assert(serialize_sensor_payload(full, payload_a, sizeof(payload_a), &len_a));
    assert(serialize_sensor_payload(full, payload_b, sizeof(payload_b), &len_b));
    assert(len_a == len_b);
    assert(std::strcmp(payload_a, payload_b) == 0);

    assert(std::strstr(payload_a, "\"temperature_c\":21.50") != nullptr);
    assert(std::strstr(payload_a, "\"occupancy\":\"occupied\"") != nullptr);
    assert(std::strstr(payload_a, "\"contact\":{\"state\":\"open\",\"tamper\":true,\"battery_low\":false}") != nullptr);
    assert(std::strstr(payload_a, "\"battery\":{\"percent\":74,\"voltage_mv\":3000}") != nullptr);
    assert(std::strstr(payload_a, "\"lqi\":201") != nullptr);
    assert(std::strstr(payload_a, "\"rssi\":-63") != nullptr);
    assert(std::strstr(payload_a, "\"stale\":false") != nullptr);
    assert(std::strstr(payload_a, "\"timestamp_ms\":4242") != nullptr);

    MqttSensorSnapshot partial{};
    partial.occupancy = service::DeviceOccupancyState::kNotOccupied;
    partial.contact_state = service::DeviceContactState::kClosed;
    partial.stale = true;
    partial.timestamp_ms = 99;

    char payload_partial[kMqttPayloadMaxLen] = {0};
    std::size_t len_partial = 0;
    assert(serialize_sensor_payload(partial, payload_partial, sizeof(payload_partial), &len_partial));
    assert(len_partial > 0U);
    assert(std::strstr(payload_partial, "\"temperature_c\":null") != nullptr);
    assert(std::strstr(payload_partial, "\"battery\":{\"percent\":null,\"voltage_mv\":null}") != nullptr);
    assert(std::strstr(payload_partial, "\"lqi\":null") != nullptr);
    assert(std::strstr(payload_partial, "\"rssi\":null") != nullptr);
    assert(std::strstr(payload_partial, "\"stale\":true") != nullptr);
    assert(std::strstr(payload_partial, "\"timestamp_ms\":99") != nullptr);

    char tiny[32] = {0};
    assert(!serialize_sensor_payload(full, tiny, sizeof(tiny), nullptr));
    assert(!serialize_sensor_payload(full, nullptr, sizeof(payload_a), nullptr));
    assert(!serialize_sensor_payload(full, payload_a, 0U, nullptr));

    return 0;
}
