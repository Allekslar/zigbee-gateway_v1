/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "mqtt_serializer_v1.hpp"

int main() {
    using namespace mqtt_bridge;

    constexpr const char* kGatewayIdHex = "0012347788aa";
    constexpr const char* kDeviceIdHex = "00124b0001aa2201";

    char payload[kV1MqttPayloadMaxLen] = {0};
    std::size_t len = 0U;

    // --- State payload ---
    assert(serialize_v1_state_payload(kGatewayIdHex, kDeviceIdHex, 7U, true, 12345U, payload, sizeof(payload), &len));
    assert(len > 0U);
    assert(std::strstr(payload, "\"schema_version\":1") != nullptr);
    assert(std::strstr(payload, "\"gateway_id\":\"0012347788aa\"") != nullptr);
    assert(std::strstr(payload, "\"device_id\":\"00124b0001aa2201\"") != nullptr);
    assert(std::strstr(payload, "\"revision\":7") != nullptr);
    assert(std::strstr(payload, "\"observed_at_ms\":12345") != nullptr);
    assert(std::strstr(payload, "\"power_on\":true") != nullptr);

    assert(serialize_v1_state_payload(kGatewayIdHex, kDeviceIdHex, 8U, false, 1U, payload, sizeof(payload), nullptr));
    assert(std::strstr(payload, "\"power_on\":false") != nullptr);

    // Rejects malformed identity.
    assert(!serialize_v1_state_payload("short", kDeviceIdHex, 7U, true, 1U, payload, sizeof(payload), &len));
    assert(!serialize_v1_state_payload(kGatewayIdHex, "short", 7U, true, 1U, payload, sizeof(payload), &len));
    assert(!serialize_v1_state_payload(kGatewayIdHex, nullptr, 7U, true, 1U, payload, sizeof(payload), &len));
    assert(!serialize_v1_state_payload(kGatewayIdHex, kDeviceIdHex, 7U, true, 1U, nullptr, sizeof(payload), &len));
    assert(!serialize_v1_state_payload(kGatewayIdHex, kDeviceIdHex, 7U, true, 1U, payload, 0U, &len));
    char tiny[16] = {0};
    assert(!serialize_v1_state_payload(kGatewayIdHex, kDeviceIdHex, 7U, true, 1U, tiny, sizeof(tiny), &len));

    // --- Availability payload ---
    assert(serialize_v1_availability_payload(
        kGatewayIdHex, kDeviceIdHex, 3U, true, 999U, payload, sizeof(payload), &len));
    assert(std::strstr(payload, "\"schema_version\":1") != nullptr);
    assert(std::strstr(payload, "\"revision\":3") != nullptr);
    assert(std::strstr(payload, "\"observed_at_ms\":999") != nullptr);
    assert(std::strstr(payload, "\"online\":true") != nullptr);

    assert(!serialize_v1_availability_payload("bad", kDeviceIdHex, 3U, true, 1U, payload, sizeof(payload), &len));

    // --- Sensor/telemetry payload (identity envelope wraps the legacy field set) ---
    MqttSensorSnapshot snapshot{};
    snapshot.has_temperature = true;
    snapshot.temperature_centi_c = 2150;
    snapshot.occupancy = service::DeviceOccupancyState::kOccupied;
    snapshot.contact_state = service::DeviceContactState::kOpen;
    snapshot.contact_tamper = true;
    snapshot.contact_battery_low = false;
    snapshot.has_battery_percent = true;
    snapshot.battery_percent = 74;
    snapshot.has_battery_voltage = true;
    snapshot.battery_voltage_mv = 3000;
    snapshot.has_lqi = true;
    snapshot.lqi = 201;
    snapshot.has_rssi = true;
    snapshot.rssi_dbm = -63;
    snapshot.stale = false;
    snapshot.timestamp_ms = 4242;

    assert(serialize_v1_sensor_payload(kGatewayIdHex, kDeviceIdHex, 42U, snapshot, payload, sizeof(payload), &len));
    assert(std::strstr(payload, "\"schema_version\":1") != nullptr);
    assert(std::strstr(payload, "\"gateway_id\":\"0012347788aa\"") != nullptr);
    assert(std::strstr(payload, "\"device_id\":\"00124b0001aa2201\"") != nullptr);
    assert(std::strstr(payload, "\"revision\":42") != nullptr);
    assert(std::strstr(payload, "\"temperature_c\":21.50") != nullptr);
    assert(std::strstr(payload, "\"occupancy\":\"occupied\"") != nullptr);
    assert(std::strstr(payload, "\"contact\":{\"state\":\"open\",\"tamper\":true,\"battery_low\":false}") != nullptr);
    assert(std::strstr(payload, "\"battery\":{\"percent\":74,\"voltage_mv\":3000}") != nullptr);
    assert(std::strstr(payload, "\"lqi\":201") != nullptr);
    assert(std::strstr(payload, "\"rssi\":-63") != nullptr);
    assert(std::strstr(payload, "\"timestamp_ms\":4242") != nullptr);

    MqttSensorSnapshot empty_snapshot{};
    assert(serialize_v1_sensor_payload(
        kGatewayIdHex, kDeviceIdHex, 1U, empty_snapshot, payload, sizeof(payload), &len));
    assert(std::strstr(payload, "\"temperature_c\":null") != nullptr);
    assert(std::strstr(payload, "\"lqi\":null") != nullptr);

    assert(!serialize_v1_sensor_payload("bad", kDeviceIdHex, 1U, snapshot, payload, sizeof(payload), &len));
    assert(!serialize_v1_sensor_payload(kGatewayIdHex, kDeviceIdHex, 1U, snapshot, nullptr, sizeof(payload), &len));

    // --- Gateway state payload (no device_id) ---
    assert(serialize_v1_gateway_state_payload(kGatewayIdHex, payload, sizeof(payload), &len));
    assert(std::strcmp(payload, "{\"schema_version\":1,\"gateway_id\":\"0012347788aa\"}") == 0);

    assert(!serialize_v1_gateway_state_payload("bad", payload, sizeof(payload), &len));
    assert(!serialize_v1_gateway_state_payload(nullptr, payload, sizeof(payload), &len));
    assert(!serialize_v1_gateway_state_payload(kGatewayIdHex, nullptr, sizeof(payload), &len));

    return 0;
}
