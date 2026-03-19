/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_mqtt.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "mqtt_client.h"

#include "log_tags.h"
#endif

#ifndef CONFIG_ZGW_MQTT_BROKER_URI
#define CONFIG_ZGW_MQTT_BROKER_URI ""
#endif

#ifndef CONFIG_ZGW_MQTT_CLIENT_ID
#define CONFIG_ZGW_MQTT_CLIENT_ID "zigbee-gateway"
#endif

#ifndef CONFIG_ZGW_MQTT_KEEPALIVE_SEC
#define CONFIG_ZGW_MQTT_KEEPALIVE_SEC 120
#endif

#ifndef CONFIG_ZGW_MQTT_NETWORK_TIMEOUT_MS
#define CONFIG_ZGW_MQTT_NETWORK_TIMEOUT_MS 30000
#endif

#ifndef CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS
#define CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS 45000
#endif

typedef struct {
    hal_mqtt_callbacks_t callbacks;
    void* context;
    bool initialized;
    bool started;
    bool connected;
#ifdef ESP_PLATFORM
    esp_mqtt_client_handle_t client;
    esp_event_handler_instance_t event_handle;
#endif
} hal_mqtt_state_t;

static hal_mqtt_state_t g_hal_mqtt = {0};

#ifdef ESP_PLATFORM
static const char* kTag = LOG_TAG_HAL_MQTT;

static const char* hal_mqtt_configured_uri(void) {
    return CONFIG_ZGW_MQTT_BROKER_URI[0] != '\0' ? CONFIG_ZGW_MQTT_BROKER_URI : "<empty>";
}

static const char* hal_mqtt_username_present(void) {
#if defined(CONFIG_ZGW_MQTT_USERNAME)
    return CONFIG_ZGW_MQTT_USERNAME[0] != '\0' ? "yes" : "no";
#else
    return "no";
#endif
}
#endif

static void hal_mqtt_reset_runtime_flags(void) {
    g_hal_mqtt.started = false;
    g_hal_mqtt.connected = false;
}

static void hal_mqtt_dispatch_connected(void) {
    if (g_hal_mqtt.callbacks.on_connected != NULL) {
        g_hal_mqtt.callbacks.on_connected(g_hal_mqtt.context);
    }
}

static void hal_mqtt_dispatch_disconnected(void) {
    if (g_hal_mqtt.callbacks.on_disconnected != NULL) {
        g_hal_mqtt.callbacks.on_disconnected(g_hal_mqtt.context);
    }
}

static void hal_mqtt_dispatch_message_common(
    const char* topic,
    size_t topic_len,
    const uint8_t* payload,
    size_t payload_len) {
    if (g_hal_mqtt.callbacks.on_message != NULL) {
        g_hal_mqtt.callbacks.on_message(g_hal_mqtt.context, topic, topic_len, payload, payload_len);
    }
}

#ifdef ESP_PLATFORM
static void hal_mqtt_dispatch_message(const esp_mqtt_event_handle_t event) {
    if (event == NULL) {
        return;
    }

    if (event->topic == NULL || event->topic_len <= 0 || event->data == NULL || event->data_len < 0) {
        return;
    }

    hal_mqtt_dispatch_message_common(
        event->topic,
        (size_t)event->topic_len,
        (const uint8_t*)event->data,
        (size_t)event->data_len);
}

static void hal_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            g_hal_mqtt.connected = true;
            ESP_LOGI(kTag, "MQTT connected uri=%s", hal_mqtt_configured_uri());
            hal_mqtt_dispatch_connected();
            break;
        case MQTT_EVENT_DISCONNECTED:
            if (g_hal_mqtt.connected) {
                ESP_LOGW(
                    kTag,
                    "MQTT session dropped after established connection uri=%s keepalive_sec=%d",
                    hal_mqtt_configured_uri(),
                    CONFIG_ZGW_MQTT_KEEPALIVE_SEC);
            }
            g_hal_mqtt.connected = false;
            ESP_LOGW(kTag, "MQTT disconnected uri=%s", hal_mqtt_configured_uri());
            hal_mqtt_dispatch_disconnected();
            break;
        case MQTT_EVENT_DATA:
            hal_mqtt_dispatch_message(event);
            break;
        case MQTT_EVENT_ERROR:
            g_hal_mqtt.connected = false;
            if (event != NULL && event->error_handle != NULL) {
                const unsigned int last_esp_err = (unsigned int)event->error_handle->esp_tls_last_esp_err;
                const unsigned int stack_err = (unsigned int)event->error_handle->esp_tls_stack_err;
                const int sock_errno = event->error_handle->esp_transport_sock_errno;
                if (event->error_handle->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
                    ESP_LOGW(
                        kTag,
                        "MQTT broker connect timeout uri=%s timeout_ms=%d reconnect_backoff_ms=%d esp_tls_stack_err=0x%x",
                        hal_mqtt_configured_uri(),
                        CONFIG_ZGW_MQTT_NETWORK_TIMEOUT_MS,
                        CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS,
                        stack_err);
                } else if (last_esp_err == 0U && sock_errno == 11) {
                    ESP_LOGW(
                        kTag,
                        "MQTT transport write stalled uri=%s reconnect_backoff_ms=%d transport_sock_errno=%d",
                        hal_mqtt_configured_uri(),
                        CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS,
                        sock_errno);
                } else {
                    ESP_LOGE(
                        kTag,
                        "MQTT error uri=%s error_type=%d esp_tls_last_esp_err=0x%x esp_tls_stack_err=0x%x transport_sock_errno=%d",
                        hal_mqtt_configured_uri(),
                        (int)event->error_handle->error_type,
                        last_esp_err,
                        stack_err,
                        sock_errno);
                }
            } else {
                ESP_LOGE(kTag, "MQTT error uri=%s", hal_mqtt_configured_uri());
            }
            break;
        default:
            break;
    }
}

static bool hal_mqtt_transport_enabled(void) {
#ifdef CONFIG_ZGW_MQTT_TRANSPORT_ENABLED
    return true;
#else
    return false;
#endif
}
#endif

static hal_mqtt_status_t hal_mqtt_copy_broker_endpoint_summary(char* out, size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return HAL_MQTT_STATUS_INVALID_ARG;
    }

    const size_t uri_len = strnlen(CONFIG_ZGW_MQTT_BROKER_URI, out_size);
    if (uri_len >= out_size) {
        return HAL_MQTT_STATUS_INVALID_ARG;
    }

    memcpy(out, CONFIG_ZGW_MQTT_BROKER_URI, uri_len);
    out[uri_len] = '\0';
    return HAL_MQTT_STATUS_OK;
}

hal_mqtt_status_t hal_mqtt_init(void) {
#ifdef ESP_PLATFORM
    if (!hal_mqtt_transport_enabled()) {
        ESP_LOGI(kTag, "MQTT transport disabled uri=%s", hal_mqtt_configured_uri());
        return HAL_MQTT_STATUS_DISABLED;
    }

    if (g_hal_mqtt.initialized) {
        return HAL_MQTT_STATUS_OK;
    }

    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = CONFIG_ZGW_MQTT_BROKER_URI;
    config.credentials.client_id = CONFIG_ZGW_MQTT_CLIENT_ID;
#if defined(CONFIG_ZGW_MQTT_USERNAME) && defined(CONFIG_ZGW_MQTT_PASSWORD)
    if (CONFIG_ZGW_MQTT_USERNAME[0] != '\0') {
        config.credentials.username = CONFIG_ZGW_MQTT_USERNAME;
    }
    if (CONFIG_ZGW_MQTT_PASSWORD[0] != '\0') {
        config.credentials.authentication.password = CONFIG_ZGW_MQTT_PASSWORD;
    }
#endif
    config.session.keepalive = CONFIG_ZGW_MQTT_KEEPALIVE_SEC;
    config.network.timeout_ms = CONFIG_ZGW_MQTT_NETWORK_TIMEOUT_MS;
    config.network.reconnect_timeout_ms = CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS;
    config.network.disable_auto_reconnect = false;

    ESP_LOGI(
        kTag,
        "Initializing MQTT transport uri=%s client_id=%s keepalive_sec=%d timeout_ms=%d reconnect_backoff_ms=%d username_present=%s",
        hal_mqtt_configured_uri(),
        CONFIG_ZGW_MQTT_CLIENT_ID,
        CONFIG_ZGW_MQTT_KEEPALIVE_SEC,
        CONFIG_ZGW_MQTT_NETWORK_TIMEOUT_MS,
        CONFIG_ZGW_MQTT_RECONNECT_TIMEOUT_MS,
        hal_mqtt_username_present());

    g_hal_mqtt.client = esp_mqtt_client_init(&config);
    if (g_hal_mqtt.client == NULL) {
        ESP_LOGE(kTag, "MQTT client init failed uri=%s", hal_mqtt_configured_uri());
        return HAL_MQTT_STATUS_FAILED;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        g_hal_mqtt.client,
        MQTT_EVENT_ANY,
        &hal_mqtt_event_handler,
        NULL);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "MQTT event registration failed uri=%s err=0x%x", hal_mqtt_configured_uri(), (unsigned int)err);
        esp_mqtt_client_destroy(g_hal_mqtt.client);
        g_hal_mqtt.client = NULL;
        return HAL_MQTT_STATUS_FAILED;
    }

    g_hal_mqtt.initialized = true;
    hal_mqtt_reset_runtime_flags();
    ESP_LOGI(kTag, "MQTT transport initialized uri=%s", hal_mqtt_configured_uri());
    return HAL_MQTT_STATUS_OK;
#else
    g_hal_mqtt.initialized = true;
    return HAL_MQTT_STATUS_OK;
#endif
}

hal_mqtt_status_t hal_mqtt_register_callbacks(const hal_mqtt_callbacks_t* callbacks, void* context) {
    if (callbacks == NULL) {
        return HAL_MQTT_STATUS_INVALID_ARG;
    }

    g_hal_mqtt.callbacks = *callbacks;
    g_hal_mqtt.context = context;
    return HAL_MQTT_STATUS_OK;
}

hal_mqtt_status_t hal_mqtt_start(void) {
#ifdef ESP_PLATFORM
    if (!hal_mqtt_transport_enabled()) {
        ESP_LOGI(kTag, "MQTT start skipped because transport is disabled uri=%s", hal_mqtt_configured_uri());
        return HAL_MQTT_STATUS_DISABLED;
    }
    if (!g_hal_mqtt.initialized || g_hal_mqtt.client == NULL) {
        return HAL_MQTT_STATUS_NOT_INITIALIZED;
    }
    if (g_hal_mqtt.started) {
        return HAL_MQTT_STATUS_OK;
    }

    ESP_LOGI(kTag, "Starting MQTT transport uri=%s", hal_mqtt_configured_uri());
    if (esp_mqtt_client_start(g_hal_mqtt.client) != ESP_OK) {
        ESP_LOGE(kTag, "MQTT start failed uri=%s", hal_mqtt_configured_uri());
        return HAL_MQTT_STATUS_FAILED;
    }

    g_hal_mqtt.started = true;
    ESP_LOGI(kTag, "MQTT start requested uri=%s", hal_mqtt_configured_uri());
    return HAL_MQTT_STATUS_OK;
#else
    if (!g_hal_mqtt.initialized) {
        return HAL_MQTT_STATUS_NOT_INITIALIZED;
    }
    g_hal_mqtt.started = true;
    g_hal_mqtt.connected = true;
    hal_mqtt_dispatch_connected();
    return HAL_MQTT_STATUS_OK;
#endif
}

hal_mqtt_status_t hal_mqtt_stop(void) {
#ifdef ESP_PLATFORM
    if (!hal_mqtt_transport_enabled()) {
        return HAL_MQTT_STATUS_DISABLED;
    }
    if (!g_hal_mqtt.initialized || g_hal_mqtt.client == NULL) {
        return HAL_MQTT_STATUS_NOT_INITIALIZED;
    }
    if (!g_hal_mqtt.started) {
        return HAL_MQTT_STATUS_OK;
    }

    if (esp_mqtt_client_stop(g_hal_mqtt.client) != ESP_OK) {
        return HAL_MQTT_STATUS_FAILED;
    }
    hal_mqtt_reset_runtime_flags();
    return HAL_MQTT_STATUS_OK;
#else
    g_hal_mqtt.started = false;
    g_hal_mqtt.connected = false;
    hal_mqtt_dispatch_disconnected();
    return HAL_MQTT_STATUS_OK;
#endif
}

bool hal_mqtt_is_connected(void) {
    return g_hal_mqtt.connected;
}

bool hal_mqtt_is_enabled(void) {
#ifdef ESP_PLATFORM
    return hal_mqtt_transport_enabled();
#else
    return true;
#endif
}

hal_mqtt_status_t hal_mqtt_get_broker_endpoint_summary(char* out, size_t out_size) {
    return hal_mqtt_copy_broker_endpoint_summary(out, out_size);
}

hal_mqtt_status_t hal_mqtt_publish(const char* topic, const char* payload, bool retain, int qos) {
    if (topic == NULL || payload == NULL || qos < 0 || qos > 2) {
        return HAL_MQTT_STATUS_INVALID_ARG;
    }
#ifdef ESP_PLATFORM
    if (!hal_mqtt_transport_enabled()) {
        return HAL_MQTT_STATUS_DISABLED;
    }
    if (!g_hal_mqtt.initialized || g_hal_mqtt.client == NULL) {
        return HAL_MQTT_STATUS_NOT_INITIALIZED;
    }

    const int message_id = esp_mqtt_client_publish(g_hal_mqtt.client, topic, payload, 0, qos, retain ? 1 : 0);
    return (message_id >= 0) ? HAL_MQTT_STATUS_OK : HAL_MQTT_STATUS_FAILED;
#else
    (void)retain;
    return HAL_MQTT_STATUS_OK;
#endif
}

hal_mqtt_status_t hal_mqtt_subscribe(const char* topic_filter, int qos) {
    if (topic_filter == NULL || topic_filter[0] == '\0' || qos < 0 || qos > 2) {
        return HAL_MQTT_STATUS_INVALID_ARG;
    }
#ifdef ESP_PLATFORM
    if (!hal_mqtt_transport_enabled()) {
        return HAL_MQTT_STATUS_DISABLED;
    }
    if (!g_hal_mqtt.initialized || g_hal_mqtt.client == NULL) {
        return HAL_MQTT_STATUS_NOT_INITIALIZED;
    }

    const int message_id = esp_mqtt_client_subscribe(g_hal_mqtt.client, topic_filter, qos);
    return (message_id >= 0) ? HAL_MQTT_STATUS_OK : HAL_MQTT_STATUS_FAILED;
#else
    return HAL_MQTT_STATUS_OK;
#endif
}
