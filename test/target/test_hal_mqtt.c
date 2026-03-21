/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_mqtt.h"
#include "unity.h"

typedef struct {
    int connected_count;
    int disconnected_count;
    int message_count;
} hal_mqtt_capture_t;

static void on_connected(void* context) {
    hal_mqtt_capture_t* capture = (hal_mqtt_capture_t*)context;
    if (capture != NULL) {
        capture->connected_count += 1;
    }
}

static void on_disconnected(void* context) {
    hal_mqtt_capture_t* capture = (hal_mqtt_capture_t*)context;
    if (capture != NULL) {
        capture->disconnected_count += 1;
    }
}

static void on_message(
    void* context,
    const char* topic,
    size_t topic_len,
    const uint8_t* payload,
    size_t payload_len) {
    hal_mqtt_capture_t* capture = (hal_mqtt_capture_t*)context;
    (void)topic;
    (void)topic_len;
    (void)payload;
    (void)payload_len;
    if (capture != NULL) {
        capture->message_count += 1;
    }
}

void test_hal_mqtt_contract(void) {
    hal_mqtt_capture_t capture = {0};
    const hal_mqtt_config_t config = {
        .broker_uri = "mqtt://broker.local:1883",
        .client_id = "zigbee-gateway-test",
        .username = "",
        .password = "",
        .keepalive_sec = 120U,
        .network_timeout_ms = 30000U,
        .reconnect_timeout_ms = 45000U,
        .auto_reconnect = true,
    };
    const hal_mqtt_callbacks_t callbacks = {
        .on_connected = on_connected,
        .on_disconnected = on_disconnected,
        .on_message = on_message,
    };

    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_INVALID_ARG, hal_mqtt_register_callbacks(NULL, NULL));

    const hal_mqtt_status_t init_status = hal_mqtt_init(&config);
    TEST_ASSERT_TRUE(init_status == HAL_MQTT_STATUS_OK || init_status == HAL_MQTT_STATUS_DISABLED);

    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_OK, hal_mqtt_register_callbacks(&callbacks, &capture));
    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_INVALID_ARG, hal_mqtt_publish(NULL, "{}", false, 1));
    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_INVALID_ARG, hal_mqtt_publish("zigbee-gateway/state", NULL, false, 1));
    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_INVALID_ARG, hal_mqtt_subscribe(NULL, 1));
    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_INVALID_ARG, hal_mqtt_get_broker_endpoint_summary(NULL, 0));

    if (init_status == HAL_MQTT_STATUS_DISABLED) {
        TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_DISABLED, hal_mqtt_start());
        TEST_ASSERT_FALSE(hal_mqtt_is_connected());
        TEST_ASSERT_FALSE(hal_mqtt_is_enabled());
        return;
    }

    char broker_summary[96] = {0};
    TEST_ASSERT_EQUAL_INT(HAL_MQTT_STATUS_OK, hal_mqtt_get_broker_endpoint_summary(broker_summary, sizeof(broker_summary)));
    TEST_ASSERT_TRUE(hal_mqtt_is_enabled());

    const hal_mqtt_status_t start_status = hal_mqtt_start();
    TEST_ASSERT_TRUE(start_status == HAL_MQTT_STATUS_OK || start_status == HAL_MQTT_STATUS_FAILED);
    const hal_mqtt_status_t subscribe_status = hal_mqtt_subscribe("zigbee-gateway/devices/+/config", 1);
    TEST_ASSERT_TRUE(subscribe_status == HAL_MQTT_STATUS_OK || subscribe_status == HAL_MQTT_STATUS_FAILED);
    (void)hal_mqtt_stop();
}
