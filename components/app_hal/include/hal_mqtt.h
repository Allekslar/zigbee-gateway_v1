/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_MQTT_STATUS_OK = 0,
    HAL_MQTT_STATUS_INVALID_ARG = 1,
    HAL_MQTT_STATUS_DISABLED = 2,
    HAL_MQTT_STATUS_NOT_INITIALIZED = 3,
    HAL_MQTT_STATUS_FAILED = 4,
} hal_mqtt_status_t;

typedef void (*hal_mqtt_on_connected_fn)(void* context);
typedef void (*hal_mqtt_on_disconnected_fn)(void* context);
typedef void (*hal_mqtt_on_message_fn)(
    void* context,
    const char* topic,
    size_t topic_len,
    const uint8_t* payload,
    size_t payload_len);

typedef struct {
    hal_mqtt_on_connected_fn on_connected;
    hal_mqtt_on_disconnected_fn on_disconnected;
    hal_mqtt_on_message_fn on_message;
} hal_mqtt_callbacks_t;

typedef struct {
    const char* broker_uri;
    const char* client_id;
    const char* username;
    const char* password;
    uint16_t keepalive_sec;
    uint32_t network_timeout_ms;
    uint32_t reconnect_timeout_ms;
    bool auto_reconnect;
} hal_mqtt_config_t;

hal_mqtt_status_t hal_mqtt_init(const hal_mqtt_config_t* config);
hal_mqtt_status_t hal_mqtt_register_callbacks(const hal_mqtt_callbacks_t* callbacks, void* context);
hal_mqtt_status_t hal_mqtt_start(void);
hal_mqtt_status_t hal_mqtt_stop(void);
bool hal_mqtt_is_connected(void);
bool hal_mqtt_is_enabled(void);
hal_mqtt_status_t hal_mqtt_get_broker_endpoint_summary(char* out, size_t out_size);
hal_mqtt_status_t hal_mqtt_publish(const char* topic, const char* payload, bool retain, int qos);
hal_mqtt_status_t hal_mqtt_subscribe(const char* topic_filter, int qos);

#ifdef __cplusplus
}
#endif
