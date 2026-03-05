/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_zigbee.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

static hal_zigbee_callbacks_t s_callbacks;
static void* s_context = 0;

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_tags.h"

#if defined(CONFIG_ZB_ENABLED) && CONFIG_ZB_ENABLED
#include "esp_err.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_core.h"
#include "esp_zigbee_endpoint.h"
#include "nwk/esp_zigbee_nwk.h"
#include "zboss_api.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "zdo/esp_zigbee_zdo_common.h"
#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
#include "esp_coexist.h"
#endif
#define HAL_ZIGBEE_HAS_ESP_ZB_SDK 1
#else
#define HAL_ZIGBEE_HAS_ESP_ZB_SDK 0
#endif

// Target adapter contract:
// This weak symbol is a placeholder only. Production ESP target builds should
// provide a strong implementation from the real Zigbee stack adapter.
static const char* kTag = LOG_TAG_HAL_ZIGBEE;
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
static bool s_stack_started = false;
static bool s_network_formed = false;
static bool s_coex_enabled = false;
static bool s_endpoint_registered = false;
static TaskHandle_t s_stack_task_handle = NULL;
static bool s_permit_join_open = false;
static int64_t s_permit_join_deadline_us = 0;
static portMUX_TYPE s_join_state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_primary_channel_mask = (1UL << 13);
static uint8_t s_max_children = 16U;
static const uint8_t kGatewayEndpoint = 1U;
static const uint8_t kDefaultOnOffEndpoint = 1U;
static const TickType_t kDeviceRemoveLockTimeout = pdMS_TO_TICKS(2000);
static const int64_t kCommandBridgeEntryTtlUs = 60LL * 1000LL * 1000LL;

typedef struct {
    bool in_use;
    uint8_t tsn;
    uint8_t command_id;
    uint16_t short_addr;
    uint32_t correlation_id;
    int64_t created_at_us;
} command_bridge_entry_t;

enum { kCommandBridgeCapacity = 16 };
static command_bridge_entry_t s_command_bridge[kCommandBridgeCapacity];
static portMUX_TYPE s_command_bridge_lock = portMUX_INITIALIZER_UNLOCKED;

static bool is_valid_short_addr(uint16_t short_addr) {
    return short_addr != 0xFFFFU && short_addr != 0x0000U;
}

static void clear_command_bridge(void) {
    portENTER_CRITICAL(&s_command_bridge_lock);
    memset(s_command_bridge, 0, sizeof(s_command_bridge));
    portEXIT_CRITICAL(&s_command_bridge_lock);
}

static bool is_command_bridge_entry_expired(const command_bridge_entry_t* entry, int64_t now_us) {
    if (entry == NULL || !entry->in_use) {
        return false;
    }
    if (now_us < entry->created_at_us) {
        return false;
    }
    return (now_us - entry->created_at_us) > kCommandBridgeEntryTtlUs;
}

static void prune_expired_command_bridge_entries_locked(int64_t now_us) {
    for (size_t i = 0; i < kCommandBridgeCapacity; ++i) {
        if (!is_command_bridge_entry_expired(&s_command_bridge[i], now_us)) {
            continue;
        }
        memset(&s_command_bridge[i], 0, sizeof(s_command_bridge[i]));
    }
}

static bool track_command_bridge_entry(
    uint8_t tsn,
    uint8_t command_id,
    uint16_t short_addr,
    uint32_t correlation_id) {
    if (correlation_id == 0U) {
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    bool evicted_oldest = false;
    command_bridge_entry_t* slot = NULL;

    portENTER_CRITICAL(&s_command_bridge_lock);
    prune_expired_command_bridge_entries_locked(now_us);

    for (size_t i = 0; i < kCommandBridgeCapacity; ++i) {
        if (s_command_bridge[i].in_use && s_command_bridge[i].tsn == tsn) {
            slot = &s_command_bridge[i];
            break;
        }
    }

    if (slot == NULL) {
        for (size_t i = 0; i < kCommandBridgeCapacity; ++i) {
            if (!s_command_bridge[i].in_use) {
                slot = &s_command_bridge[i];
                break;
            }
        }
    }

    if (slot == NULL) {
        slot = &s_command_bridge[0];
        for (size_t i = 1; i < kCommandBridgeCapacity; ++i) {
            if (s_command_bridge[i].created_at_us < slot->created_at_us) {
                slot = &s_command_bridge[i];
            }
        }
        evicted_oldest = true;
    }

    slot->in_use = true;
    slot->tsn = tsn;
    slot->command_id = command_id;
    slot->short_addr = short_addr;
    slot->correlation_id = correlation_id;
    slot->created_at_us = now_us;
    portEXIT_CRITICAL(&s_command_bridge_lock);

    if (evicted_oldest) {
        ESP_LOGW(
            kTag,
            "Command bridge full, evicted oldest entry to track tsn=%u correlation_id=%lu",
            (unsigned)tsn,
            (unsigned long)correlation_id);
    }
    return true;
}

static bool consume_command_bridge_entry(uint8_t tsn, command_bridge_entry_t* out_entry) {
    if (out_entry == NULL) {
        return false;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    const int64_t now_us = esp_timer_get_time();
    bool found = false;

    portENTER_CRITICAL(&s_command_bridge_lock);
    prune_expired_command_bridge_entries_locked(now_us);

    for (size_t i = 0; i < kCommandBridgeCapacity; ++i) {
        if (!s_command_bridge[i].in_use || s_command_bridge[i].tsn != tsn) {
            continue;
        }

        *out_entry = s_command_bridge[i];
        memset(&s_command_bridge[i], 0, sizeof(s_command_bridge[i]));
        found = true;
        break;
    }
    portEXIT_CRITICAL(&s_command_bridge_lock);
    return found;
}

static hal_zigbee_result_t map_send_status_to_hal_result(esp_err_t status) {
    if (status == ESP_OK) {
        return HAL_ZIGBEE_RESULT_SUCCESS;
    }
    if (status == ESP_ERR_TIMEOUT) {
        return HAL_ZIGBEE_RESULT_TIMEOUT;
    }
    return HAL_ZIGBEE_RESULT_FAILED;
}

static hal_zigbee_result_t map_zcl_status_to_hal_result(esp_zb_zcl_status_t status) {
    if (status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        return HAL_ZIGBEE_RESULT_SUCCESS;
    }
    if (status == ESP_ZB_ZCL_STATUS_TIMEOUT) {
        return HAL_ZIGBEE_RESULT_TIMEOUT;
    }
    return HAL_ZIGBEE_RESULT_FAILED;
}

static void zigbee_command_send_status_handler(esp_zb_zcl_command_send_status_message_t message) {
    if (message.status == ESP_OK) {
        return;
    }

    command_bridge_entry_t bridge_entry = {0};
    if (!consume_command_bridge_entry(message.tsn, &bridge_entry)) {
        ESP_LOGW(
            kTag,
            "send_status without tracked TSN tsn=%u status=%s",
            (unsigned)message.tsn,
            esp_err_to_name(message.status));
        return;
    }

    const hal_zigbee_result_t result = map_send_status_to_hal_result(message.status);
    ESP_LOGW(
        kTag,
        "On/Off send failed short_addr=0x%04x tsn=%u correlation_id=%lu status=%s",
        (unsigned)bridge_entry.short_addr,
        (unsigned)message.tsn,
        (unsigned long)bridge_entry.correlation_id,
        esp_err_to_name(message.status));
    hal_zigbee_notify_command_result(bridge_entry.correlation_id, result);
}

static esp_err_t zigbee_core_action_handler(esp_zb_core_action_callback_id_t callback_id, const void* message) {
    if (callback_id != ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID) {
        return ESP_OK;
    }

    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_zb_zcl_cmd_default_resp_message_t* default_resp =
        (const esp_zb_zcl_cmd_default_resp_message_t*)message;
    const uint8_t tsn = default_resp->info.header.tsn;

    command_bridge_entry_t bridge_entry = {0};
    if (!consume_command_bridge_entry(tsn, &bridge_entry)) {
        ESP_LOGW(
            kTag,
            "default_resp without tracked TSN tsn=%u cmd=0x%02x status=0x%02x",
            (unsigned)tsn,
            (unsigned)default_resp->resp_to_cmd,
            (unsigned)default_resp->status_code);
        return ESP_OK;
    }

    const hal_zigbee_result_t result = map_zcl_status_to_hal_result(default_resp->status_code);
    ESP_LOGI(
        kTag,
        "On/Off result short_addr=0x%04x tsn=%u correlation_id=%lu zcl_status=0x%02x",
        (unsigned)bridge_entry.short_addr,
        (unsigned)tsn,
        (unsigned long)bridge_entry.correlation_id,
        (unsigned)default_resp->status_code);
    hal_zigbee_notify_command_result(bridge_entry.correlation_id, result);
    return ESP_OK;
}

static void set_join_window_state(bool open, uint8_t duration_seconds) {
    portENTER_CRITICAL(&s_join_state_lock);
    s_permit_join_open = open;
    if (open && duration_seconds > 0U) {
        s_permit_join_deadline_us =
            esp_timer_get_time() + (int64_t)duration_seconds * 1000LL * 1000LL;
    } else {
        s_permit_join_deadline_us = 0;
    }
    portEXIT_CRITICAL(&s_join_state_lock);
}

static void notify_join_with_source(uint16_t short_addr, const char* source_tag) {
    if (!is_valid_short_addr(short_addr)) {
        ESP_LOGW(kTag, "Ignore join from %s with invalid short_addr=0x%04x", source_tag, (unsigned)short_addr);
        return;
    }

    ESP_LOGI(kTag, "Join candidate from %s short_addr=0x%04x", source_tag, (unsigned)short_addr);
    hal_zigbee_notify_device_joined(short_addr);
}

static void notify_left_with_source(uint16_t short_addr, const char* source_tag) {
    if (!is_valid_short_addr(short_addr)) {
        ESP_LOGW(kTag, "Ignore leave from %s with invalid short_addr=0x%04x", source_tag, (unsigned)short_addr);
        return;
    }

    ESP_LOGI(kTag, "Leave candidate from %s short_addr=0x%04x", source_tag, (unsigned)short_addr);
    hal_zigbee_notify_device_left(short_addr);
}

static uint16_t resolve_short_from_ieee(esp_zb_ieee_addr_t ieee_addr) {
    return esp_zb_address_short_by_ieee(ieee_addr);
}

static void leave_request_result_cb(esp_zb_zdp_status_t zdo_status, void* user_ctx) {
    const uint32_t raw = (uint32_t)(uintptr_t)user_ctx;
    const uint16_t short_addr = (uint16_t)(raw & 0xFFFFU);
    ESP_LOGI(
        kTag,
        "Mgmt_Leave response short_addr=0x%04x zdo_status=0x%02x",
        (unsigned)short_addr,
        (unsigned)zdo_status);
}

static int register_gateway_endpoint_if_needed(void) {
    if (s_endpoint_registered) {
        return 0;
    }

    esp_zb_ep_list_t* ep_list = esp_zb_ep_list_create();
    if (ep_list == NULL) {
        ESP_LOGE(kTag, "esp_zb_ep_list_create failed");
        return -1;
    }

    esp_zb_cluster_list_t* cluster_list = esp_zb_zcl_cluster_list_create();
    if (cluster_list == NULL) {
        ESP_LOGE(kTag, "esp_zb_zcl_cluster_list_create failed");
        return -1;
    }

    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = kGatewayEndpoint,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
    };

    esp_zb_attribute_list_t* basic_cluster = esp_zb_basic_cluster_create(NULL);
    if (basic_cluster == NULL) {
        ESP_LOGE(kTag, "esp_zb_basic_cluster_create failed");
        return -1;
    }
    if (esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) !=
        ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_cluster_list_add_basic_cluster failed");
        return -1;
    }

    esp_zb_attribute_list_t* identify_cluster = esp_zb_identify_cluster_create(NULL);
    if (identify_cluster == NULL) {
        ESP_LOGE(kTag, "esp_zb_identify_cluster_create failed");
        return -1;
    }
    if (esp_zb_cluster_list_add_identify_cluster(
            cluster_list, identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_cluster_list_add_identify_cluster failed");
        return -1;
    }

    esp_zb_attribute_list_t* on_off_cluster = esp_zb_on_off_cluster_create(NULL);
    if (on_off_cluster == NULL) {
        ESP_LOGE(kTag, "esp_zb_on_off_cluster_create failed");
        return -1;
    }
    if (esp_zb_cluster_list_add_on_off_cluster(cluster_list, on_off_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE) !=
        ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_cluster_list_add_on_off_cluster failed");
        return -1;
    }

    if (esp_zb_ep_list_add_gateway_ep(ep_list, cluster_list, endpoint_config) != ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_ep_list_add_gateway_ep failed");
        return -1;
    }

    if (esp_zb_device_register(ep_list) != ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_device_register failed");
        return -1;
    }

    s_endpoint_registered = true;
    ESP_LOGI(kTag, "Gateway endpoint registered: ep=%u", (unsigned)kGatewayEndpoint);
    return 0;
}

static int start_zigbee_stack_if_needed(void);

static void zigbee_stack_task(void* arg) {
    (void)arg;
    ESP_LOGI(kTag, "Zigbee stack task started");

    if (start_zigbee_stack_if_needed() != 0) {
        ESP_LOGE(kTag, "Zigbee stack init/start failed in task");
        s_stack_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_zb_stack_main_loop();
    ESP_LOGW(kTag, "Zigbee stack task exited unexpectedly");
    s_stack_started = false;
    s_stack_task_handle = NULL;
    vTaskDelete(NULL);
}
#endif
hal_zigbee_status_t __attribute__((weak))
hal_zigbee_stack_send_on_off(uint32_t correlation_id, uint16_t short_addr, bool on) {
    (void)correlation_id;
    (void)short_addr;
    (void)on;
    ESP_LOGE(kTag, "Real Zigbee target adapter is not linked (hal_zigbee_stack_send_on_off)");
    return HAL_ZIGBEE_STATUS_ERR;
}

hal_zigbee_status_t __attribute__((weak)) hal_zigbee_stack_open_network(uint16_t duration_seconds) {
    (void)duration_seconds;
    ESP_LOGE(kTag, "Real Zigbee target adapter is not linked (hal_zigbee_stack_open_network)");
    return HAL_ZIGBEE_STATUS_ERR;
}

hal_zigbee_status_t __attribute__((weak)) hal_zigbee_stack_close_network(void) {
    ESP_LOGE(kTag, "Real Zigbee target adapter is not linked (hal_zigbee_stack_close_network)");
    return HAL_ZIGBEE_STATUS_ERR;
}

bool __attribute__((weak)) hal_zigbee_stack_adapter_linked(void) {
    return false;
}

#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
static int start_zigbee_stack_if_needed(void) {
    if (s_stack_started) {
        return 0;
    }

    esp_zb_platform_config_t platform_cfg = {
        .radio_config =
            {
                .radio_mode = ZB_RADIO_MODE_NATIVE,
            },
        .host_config =
            {
                .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
            },
    };

    const esp_err_t platform_err = esp_zb_platform_config(&platform_cfg);
    if (platform_err != ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_platform_config failed: %s", esp_err_to_name(platform_err));
        return -1;
    }

    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg = {
            .zczr_cfg = {
                .max_children = s_max_children,
            },
        },
    };

    esp_zb_init(&zb_cfg);

    const esp_err_t channel_err = esp_zb_set_primary_network_channel_set(s_primary_channel_mask);
    if (channel_err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to set primary channel mask: %s", esp_err_to_name(channel_err));
    } else {
        ESP_LOGI(kTag, "Primary Zigbee channel mask set to 0x%08lx", (unsigned long)s_primary_channel_mask);
    }

    if (register_gateway_endpoint_if_needed() != 0) {
        return -1;
    }

    clear_command_bridge();
    esp_zb_core_action_handler_register(zigbee_core_action_handler);
    esp_zb_zcl_command_send_status_handler_register(zigbee_command_send_status_handler);

    const esp_err_t start_err = esp_zb_start(false);
    if (start_err != ESP_OK) {
        ESP_LOGE(kTag, "esp_zb_start(false) failed: %s", esp_err_to_name(start_err));
        return -1;
    }

    s_stack_started = true;
    ESP_LOGI(kTag, "Zigbee stack started in no-autostart mode");
    return 0;
}

#endif
#endif

hal_zigbee_status_t hal_zigbee_init(void) {
#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (s_stack_task_handle == NULL) {
        s_network_formed = false;
        s_coex_enabled = false;
        set_join_window_state(false, 0U);

        const BaseType_t task_ok = xTaskCreate(
            zigbee_stack_task,
            "zigbee_main",
            8192,
            NULL,
            5,
            &s_stack_task_handle);
        if (task_ok != pdPASS) {
            ESP_LOGE(kTag, "Failed to create Zigbee stack task");
            return HAL_ZIGBEE_STATUS_ERR;
        }
    }
#else
    ESP_LOGE(kTag, "Real Zigbee target adapter is not linked (hal_zigbee_init)");
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#endif
    return HAL_ZIGBEE_STATUS_OK;
}

hal_zigbee_status_t hal_zigbee_set_primary_channel_mask(uint32_t channel_mask) {
    if (channel_mask == 0U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (s_stack_started || s_stack_task_handle != NULL) {
        return HAL_ZIGBEE_STATUS_ERR;
    }
    s_primary_channel_mask = channel_mask;
    return HAL_ZIGBEE_STATUS_OK;
#else
    (void)channel_mask;
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#else
    (void)channel_mask;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_set_max_children(uint8_t max_children) {
    if (max_children == 0U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (s_stack_started || s_stack_task_handle != NULL) {
        return HAL_ZIGBEE_STATUS_ERR;
    }
    s_max_children = max_children;
    return HAL_ZIGBEE_STATUS_OK;
#else
    (void)max_children;
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#else
    (void)max_children;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_register_callbacks(const hal_zigbee_callbacks_t* callbacks, void* context) {
    if (callbacks == 0) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

    s_callbacks = *callbacks;
    s_context = context;
    return HAL_ZIGBEE_STATUS_OK;
}

hal_zigbee_status_t hal_zigbee_send_on_off(uint32_t correlation_id, uint16_t short_addr, bool on) {
#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!is_valid_short_addr(short_addr)) {
        ESP_LOGE(kTag, "Reject on/off command for invalid short_addr=0x%04x", (unsigned)short_addr);
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

    if (!s_stack_started || !esp_zb_is_started()) {
        ESP_LOGE(kTag, "Cannot send on/off to 0x%04x: Zigbee stack not started", (unsigned)short_addr);
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }

    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(2000))) {
        ESP_LOGE(kTag, "Failed to acquire Zigbee lock for on/off short_addr=0x%04x", (unsigned)short_addr);
        return HAL_ZIGBEE_STATUS_ERR;
    }

    esp_zb_zcl_on_off_cmd_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = short_addr,
            .dst_endpoint = kDefaultOnOffEndpoint,
            .src_endpoint = kGatewayEndpoint,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .on_off_cmd_id = on ? ESP_ZB_ZCL_CMD_ON_OFF_ON_ID : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID,
    };
    const uint8_t tsn = esp_zb_zcl_on_off_cmd_req(&cmd_req);
    const bool tracked = track_command_bridge_entry(tsn, cmd_req.on_off_cmd_id, short_addr, correlation_id);
    esp_zb_lock_release();

    ESP_LOGI(
        kTag,
        "On/Off command sent short_addr=0x%04x dst_ep=%u cmd=%s tsn=%u correlation_id=%lu",
        (unsigned)short_addr,
        (unsigned)kDefaultOnOffEndpoint,
        on ? "ON" : "OFF",
        (unsigned)tsn,
        (unsigned long)correlation_id);

    if (!tracked) {
        ESP_LOGE(
            kTag,
            "Failed to track command result bridge short_addr=0x%04x tsn=%u correlation_id=%lu",
            (unsigned)short_addr,
            (unsigned)tsn,
            (unsigned long)correlation_id);
        hal_zigbee_notify_command_result(correlation_id, HAL_ZIGBEE_RESULT_FAILED);
        return HAL_ZIGBEE_STATUS_ERR;
    }

    // Command completion is reported asynchronously via on_command_result callback.
    // Do not emit synthetic SUCCESS on enqueue.
    return HAL_ZIGBEE_STATUS_OK;
#else
    return hal_zigbee_stack_send_on_off(correlation_id, short_addr, on);
#endif
#else
    // TEMP MOCK PATH (!ESP_PLATFORM):
    // Host-only behavior for tests; enqueue succeeds, completion is still async.
    (void)correlation_id;
    (void)short_addr;
    (void)on;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_request_interview(uint32_t correlation_id, uint16_t short_addr) {
    if (correlation_id == 0U || short_addr == 0xFFFFU || short_addr == 0x0000U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }
    ESP_LOGI(
        kTag,
        "Interview requested short_addr=0x%04x correlation_id=%lu",
        (unsigned)short_addr,
        (unsigned long)correlation_id);
    return HAL_ZIGBEE_STATUS_OK;
#else
    (void)correlation_id;
    (void)short_addr;
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#else
    (void)correlation_id;
    (void)short_addr;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_request_bind(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t src_endpoint,
    uint16_t cluster_id,
    uint8_t dst_endpoint) {
    if (correlation_id == 0U || short_addr == 0xFFFFU || short_addr == 0x0000U || src_endpoint == 0U ||
        dst_endpoint == 0U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }
    ESP_LOGI(
        kTag,
        "Bind requested short_addr=0x%04x src_ep=%u cluster=0x%04x dst_ep=%u correlation_id=%lu",
        (unsigned)short_addr,
        (unsigned)src_endpoint,
        (unsigned)cluster_id,
        (unsigned)dst_endpoint,
        (unsigned long)correlation_id);
    return HAL_ZIGBEE_STATUS_OK;
#else
    (void)correlation_id;
    (void)short_addr;
    (void)src_endpoint;
    (void)cluster_id;
    (void)dst_endpoint;
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#else
    (void)correlation_id;
    (void)short_addr;
    (void)src_endpoint;
    (void)cluster_id;
    (void)dst_endpoint;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_request_configure_reporting(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint16_t min_interval_seconds,
    uint16_t max_interval_seconds,
    uint32_t reportable_change) {
    if (correlation_id == 0U || short_addr == 0xFFFFU || short_addr == 0x0000U || endpoint == 0U ||
        min_interval_seconds > max_interval_seconds) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }
    ESP_LOGI(
        kTag,
        "Configure reporting requested short_addr=0x%04x ep=%u cluster=0x%04x attr=0x%04x min=%u max=%u change=%lu corr=%lu",
        (unsigned)short_addr,
        (unsigned)endpoint,
        (unsigned)cluster_id,
        (unsigned)attribute_id,
        (unsigned)min_interval_seconds,
        (unsigned)max_interval_seconds,
        (unsigned long)reportable_change,
        (unsigned long)correlation_id);
    return HAL_ZIGBEE_STATUS_OK;
#else
    (void)correlation_id;
    (void)short_addr;
    (void)endpoint;
    (void)cluster_id;
    (void)attribute_id;
    (void)min_interval_seconds;
    (void)max_interval_seconds;
    (void)reportable_change;
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#else
    (void)correlation_id;
    (void)short_addr;
    (void)endpoint;
    (void)cluster_id;
    (void)attribute_id;
    (void)min_interval_seconds;
    (void)max_interval_seconds;
    (void)reportable_change;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_request_read_attribute(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id) {
    if (correlation_id == 0U || short_addr == 0xFFFFU || short_addr == 0x0000U || endpoint == 0U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }
    ESP_LOGI(
        kTag,
        "Read attribute requested short_addr=0x%04x ep=%u cluster=0x%04x attr=0x%04x correlation_id=%lu",
        (unsigned)short_addr,
        (unsigned)endpoint,
        (unsigned)cluster_id,
        (unsigned)attribute_id,
        (unsigned long)correlation_id);
    return HAL_ZIGBEE_STATUS_OK;
#else
    (void)correlation_id;
    (void)short_addr;
    (void)endpoint;
    (void)cluster_id;
    (void)attribute_id;
    return HAL_ZIGBEE_STATUS_NOT_LINKED;
#endif
#else
    (void)correlation_id;
    (void)short_addr;
    (void)endpoint;
    (void)cluster_id;
    (void)attribute_id;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_start_network_formation(void) {
#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        ESP_LOGI(kTag, "Cannot start network formation yet: Zigbee stack not started");
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }

    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(2000))) {
        ESP_LOGE(kTag, "Failed to acquire Zigbee lock for network formation");
        return HAL_ZIGBEE_STATUS_ERR;
    }

    const esp_err_t err = esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
    esp_zb_lock_release();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start network formation: %s", esp_err_to_name(err));
        return HAL_ZIGBEE_STATUS_ERR;
    }

    s_network_formed = false;
    ESP_LOGI(kTag, "Network formation requested");
    return HAL_ZIGBEE_STATUS_OK;
#else
    ESP_LOGE(
        kTag,
        "Zigbee disabled in Kconfig (CONFIG_ZB_ENABLED=n), cannot start network formation");
    return HAL_ZIGBEE_STATUS_ERR;
#endif
#else
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_open_network(uint16_t duration_seconds) {
    if (duration_seconds == 0U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        ESP_LOGE(kTag, "Cannot open network: Zigbee stack not started");
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }

    if (!s_network_formed) {
        ESP_LOGW(kTag, "Cannot open network: Zigbee network is not formed yet");
        return HAL_ZIGBEE_STATUS_NETWORK_NOT_FORMED;
    }

    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(2000))) {
        ESP_LOGE(kTag, "Failed to acquire Zigbee lock for open network");
        return HAL_ZIGBEE_STATUS_ERR;
    }

    const esp_err_t err = esp_zb_bdb_open_network((uint8_t)duration_seconds);
    esp_zb_lock_release();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to open Zigbee network: %s", esp_err_to_name(err));
        return HAL_ZIGBEE_STATUS_ERR;
    }
    set_join_window_state(true, (uint8_t)duration_seconds);
    ESP_LOGI(kTag, "Zigbee join window opened for %u seconds", (unsigned)duration_seconds);
    return HAL_ZIGBEE_STATUS_OK;
#else
    ESP_LOGE(
        kTag,
        "Zigbee disabled in Kconfig (CONFIG_ZB_ENABLED=n), fallback adapter path");
    return hal_zigbee_stack_open_network(duration_seconds);
#endif
#else
    // TEMP MOCK PATH (!ESP_PLATFORM):
    // Host-only behavior for tests; emulate success for join-window request.
    (void)duration_seconds;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_close_network(void) {
#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        set_join_window_state(false, 0U);
        ESP_LOGW(kTag, "Zigbee stack not started yet, close-network treated as no-op");
        return HAL_ZIGBEE_STATUS_OK;
    }

    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(2000))) {
        ESP_LOGE(kTag, "Failed to acquire Zigbee lock for close network");
        return HAL_ZIGBEE_STATUS_ERR;
    }

    const esp_err_t err = esp_zb_bdb_close_network();
    esp_zb_lock_release();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to close Zigbee join window: %s", esp_err_to_name(err));
        return HAL_ZIGBEE_STATUS_ERR;
    }

    set_join_window_state(false, 0U);
    ESP_LOGI(kTag, "Zigbee join window closed by request");
    return HAL_ZIGBEE_STATUS_OK;
#else
    return hal_zigbee_stack_close_network();
#endif
#else
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_remove_device(uint16_t short_addr) {
    if (short_addr == 0xFFFFU || short_addr == 0x0000U) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    if (!s_stack_started || !esp_zb_is_started()) {
        ESP_LOGE(kTag, "Cannot remove device 0x%04x: Zigbee stack not started", (unsigned)short_addr);
        return HAL_ZIGBEE_STATUS_NOT_STARTED;
    }

    if (!esp_zb_lock_acquire(kDeviceRemoveLockTimeout)) {
        ESP_LOGE(kTag, "Failed to acquire Zigbee lock for remove device 0x%04x", (unsigned)short_addr);
        return HAL_ZIGBEE_STATUS_ERR;
    }

    esp_zb_ieee_addr_t ieee_addr = {0};
    const esp_err_t ieee_err = esp_zb_ieee_address_by_short(short_addr, ieee_addr);
    if (ieee_err != ESP_OK) {
        esp_zb_lock_release();
        ESP_LOGE(
            kTag,
            "Failed to resolve IEEE for short_addr=0x%04x: %s",
            (unsigned)short_addr,
            esp_err_to_name(ieee_err));
        return HAL_ZIGBEE_STATUS_ERR;
    }

    esp_zb_zdo_mgmt_leave_req_param_t leave_req = {
        .device_address = {0},
        .dst_nwk_addr = short_addr,
        .reserved = 0U,
        .remove_children = 0U,
        .rejoin = 0U,
    };
    memcpy(leave_req.device_address, ieee_addr, sizeof(leave_req.device_address));

    esp_zb_zdo_device_leave_req(
        &leave_req,
        leave_request_result_cb,
        (void*)(uintptr_t)short_addr);
    esp_zb_lock_release();

    ESP_LOGI(kTag, "Mgmt_Leave sent short_addr=0x%04x", (unsigned)short_addr);
    return HAL_ZIGBEE_STATUS_OK;
#else
    ESP_LOGE(
        kTag,
        "Zigbee disabled in Kconfig (CONFIG_ZB_ENABLED=n), cannot remove device");
    return HAL_ZIGBEE_STATUS_ERR;
#endif
#else
    (void)short_addr;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

hal_zigbee_status_t hal_zigbee_get_join_window_status(bool* open, uint16_t* seconds_left) {
    if (open == NULL || seconds_left == NULL) {
        return HAL_ZIGBEE_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    bool open_local = false;
    int64_t deadline_us = 0;
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_join_state_lock);
    open_local = s_permit_join_open;
    deadline_us = s_permit_join_deadline_us;
    portEXIT_CRITICAL(&s_join_state_lock);

    if (open_local && deadline_us > 0 && now_us >= deadline_us) {
        set_join_window_state(false, 0U);
        open_local = false;
        deadline_us = 0;
    }

    uint16_t remaining_seconds = 0;
    if (open_local && deadline_us > now_us) {
        const int64_t remaining_us = deadline_us - now_us;
        remaining_seconds = (uint16_t)((remaining_us + 999999LL) / 1000000LL);
        if (remaining_seconds == 0U) {
            remaining_seconds = 1U;
        }
    }

    *open = open_local;
    *seconds_left = remaining_seconds;
    return HAL_ZIGBEE_STATUS_OK;
#else
    *open = false;
    *seconds_left = 0U;
    return HAL_ZIGBEE_STATUS_ERR;
#endif
#else
    *open = false;
    *seconds_left = 0U;
    return HAL_ZIGBEE_STATUS_OK;
#endif
}

bool hal_zigbee_is_network_formed(void) {
#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    return s_network_formed;
#else
    return false;
#endif
#else
    return true;
#endif
}

void hal_zigbee_poll(void) {
#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
    // Main loop is driven by dedicated Zigbee task.
#endif
#endif
}

#ifdef ESP_PLATFORM
#if HAL_ZIGBEE_HAS_ESP_ZB_SDK
void esp_zb_app_signal_handler(esp_zb_app_signal_t* signal_s) {
    if (signal_s == 0 || signal_s->p_app_signal == 0) {
        ESP_LOGW(kTag, "Received empty Zigbee app signal");
        return;
    }

    const esp_zb_app_signal_type_t signal_type =
        (esp_zb_app_signal_type_t)(*signal_s->p_app_signal);
    const esp_err_t status = signal_s->esp_err_status;

    ESP_LOGI(
        kTag,
        "Zigbee app signal: %s (%u), status=%s",
        esp_zb_zdo_signal_to_string(signal_type),
        (unsigned)signal_type,
        esp_err_to_name(status));

    switch (signal_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP: {
            if (status != ESP_OK) {
                ESP_LOGE(kTag, "Skip startup failed: %s", esp_err_to_name(status));
                return;
            }

#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
            if (!s_coex_enabled) {
                const esp_err_t coex_err = esp_coex_wifi_i154_enable();
                if (coex_err == ESP_OK) {
                    s_coex_enabled = true;
                    ESP_LOGI(kTag, "Wi-Fi/802.15.4 software coexistence enabled");
                } else {
                    ESP_LOGW(kTag, "Failed to enable Wi-Fi/802.15.4 coexistence: %s", esp_err_to_name(coex_err));
                }
            }
#endif

            const esp_err_t err =
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "BDB initialization commissioning failed: %s", esp_err_to_name(err));
            }
            return;
        }
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT: {
            if (status != ESP_OK) {
                ESP_LOGE(kTag, "Failed to initialize Zigbee stack: %s", esp_err_to_name(status));
                return;
            }

            if (esp_zb_bdb_is_factory_new()) {
                s_network_formed = false;
                ESP_LOGI(kTag, "Factory-new coordinator, waiting for service to request network formation");
            } else {
                s_network_formed = true;
                ESP_LOGI(kTag, "Coordinator restored existing Zigbee network");
            }
            return;
        }
        case ESP_ZB_BDB_SIGNAL_FORMATION: {
            if (status == ESP_OK) {
                s_network_formed = true;
                ESP_LOGI(
                    kTag,
                    "Zigbee network formed pan_id=0x%04x channel=%u",
                    (unsigned)esp_zb_get_pan_id(),
                    (unsigned)esp_zb_get_current_channel());
            } else {
                s_network_formed = false;
                ESP_LOGW(kTag, "Network formation failed");
            }
            return;
        }
        case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
            if (status == ESP_OK) {
                const uint8_t* permit_join_seconds =
                    (const uint8_t*)esp_zb_app_signal_get_params(signal_s->p_app_signal);
                if (permit_join_seconds != 0 && *permit_join_seconds != 0U) {
                    set_join_window_state(true, *permit_join_seconds);
                    ESP_LOGI(
                        kTag,
                        "Network permit-join enabled for %u seconds (pan_id=0x%04x ch=%u)",
                        (unsigned)*permit_join_seconds,
                        (unsigned)esp_zb_get_pan_id(),
                        (unsigned)esp_zb_get_current_channel());
                } else {
                    set_join_window_state(false, 0U);
                    ESP_LOGI(
                        kTag,
                        "Network permit-join closed (pan_id=0x%04x ch=%u)",
                        (unsigned)esp_zb_get_pan_id(),
                        (unsigned)esp_zb_get_current_channel());
                }
            }
            return;
        }
        case ESP_ZB_NWK_SIGNAL_DEVICE_ASSOCIATED: {
            if (status == ESP_OK) {
                esp_zb_nwk_signal_device_associated_params_t* params =
                    (esp_zb_nwk_signal_device_associated_params_t*)esp_zb_app_signal_get_params(
                        signal_s->p_app_signal);
                if (params != 0) {
                    const uint16_t resolved_short = resolve_short_from_ieee(params->device_addr);
                    ESP_LOGI(
                        kTag,
                        "Device associated, resolved short_addr=0x%04x",
                        (unsigned)resolved_short);
                    if (is_valid_short_addr(resolved_short)) {
                        notify_join_with_source(resolved_short, "DEVICE_ASSOCIATED");
                    } else {
                        ESP_LOGW(kTag, "DEVICE_ASSOCIATED without short_addr yet, waiting for next signal");
                    }
                }
            }
            return;
        }
        case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
            if (status == ESP_OK) {
                esp_zb_zdo_signal_device_annce_params_t* params =
                    (esp_zb_zdo_signal_device_annce_params_t*)esp_zb_app_signal_get_params(
                        signal_s->p_app_signal);
                if (params != 0) {
                    ESP_LOGI(
                        kTag,
                        "Device announce received short_addr=0x%04x",
                        (unsigned)params->device_short_addr);
                    notify_join_with_source(params->device_short_addr, "DEVICE_ANNCE");
                }
            }
            return;
        }
        case ESP_ZB_ZDO_SIGNAL_DEVICE_AUTHORIZED: {
            if (status == ESP_OK) {
                esp_zb_zdo_signal_device_authorized_params_t* params =
                    (esp_zb_zdo_signal_device_authorized_params_t*)esp_zb_app_signal_get_params(
                        signal_s->p_app_signal);
                if (params != 0) {
                    ESP_LOGI(
                        kTag,
                        "Device authorized short_addr=0x%04x auth_type=%u auth_status=%u",
                        (unsigned)params->short_addr,
                        (unsigned)params->authorization_type,
                        (unsigned)params->authorization_status);
                    if (params->authorization_status == 0U) {
                        notify_join_with_source(params->short_addr, "DEVICE_AUTHORIZED");
                    } else {
                        ESP_LOGW(
                            kTag,
                            "DEVICE_AUTHORIZED reports non-success auth_status=%u short_addr=0x%04x",
                            (unsigned)params->authorization_status,
                            (unsigned)params->short_addr);
                    }
                }
            }
            return;
        }
        case ESP_ZB_ZDO_SIGNAL_DEVICE_UPDATE: {
            if (status == ESP_OK) {
                esp_zb_zdo_signal_device_update_params_t* params =
                    (esp_zb_zdo_signal_device_update_params_t*)esp_zb_app_signal_get_params(
                        signal_s->p_app_signal);
                if (params != 0) {
                    ESP_LOGI(
                        kTag,
                        "Device update short_addr=0x%04x status=%u tc_action=%u parent=0x%04x",
                        (unsigned)params->short_addr,
                        (unsigned)params->status,
                        (unsigned)params->tc_action,
                        (unsigned)params->parent_short);

                    if (params->status == ESP_ZB_ZDO_STANDARD_DEV_LEFT) {
                        notify_left_with_source(params->short_addr, "DEVICE_UPDATE");
                    } else {
                        notify_join_with_source(params->short_addr, "DEVICE_UPDATE");
                    }
                }
            }
            return;
        }
        case ESP_ZB_ZDO_SIGNAL_LEAVE_INDICATION: {
            if (status == ESP_OK) {
                esp_zb_zdo_signal_leave_indication_params_t* params =
                    (esp_zb_zdo_signal_leave_indication_params_t*)esp_zb_app_signal_get_params(
                        signal_s->p_app_signal);
                if (params != 0) {
                    ESP_LOGI(
                        kTag,
                        "Leave indication received short_addr=0x%04x",
                        (unsigned)params->short_addr);
                    notify_left_with_source(params->short_addr, "LEAVE_INDICATION");
                }
            }
            return;
        }
        default:
            return;
    }
}
#endif
#endif

void hal_zigbee_notify_device_joined(uint16_t short_addr) {
    if (s_callbacks.on_device_joined != 0) {
        s_callbacks.on_device_joined(s_context, short_addr);
    }
}

void hal_zigbee_notify_device_left(uint16_t short_addr) {
    if (s_callbacks.on_device_left != 0) {
        s_callbacks.on_device_left(s_context, short_addr);
    }
}

void hal_zigbee_notify_attribute_report(
    uint16_t short_addr,
    uint16_t cluster_id,
    uint16_t attribute_id,
    bool value_bool,
    uint32_t value_u32) {
    if (s_callbacks.on_attribute_report != 0) {
        s_callbacks.on_attribute_report(
            s_context,
            short_addr,
            cluster_id,
            attribute_id,
            value_bool,
            value_u32);
    }
}

void hal_zigbee_notify_command_result(uint32_t correlation_id, hal_zigbee_result_t result) {
    if (s_callbacks.on_command_result != 0) {
        s_callbacks.on_command_result(s_context, correlation_id, result);
    }
}

void hal_zigbee_notify_interview_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
    if (s_callbacks.on_interview_result != 0) {
        s_callbacks.on_interview_result(s_context, correlation_id, short_addr, result);
    }
}

void hal_zigbee_notify_bind_result(uint32_t correlation_id, uint16_t short_addr, hal_zigbee_result_t result) {
    if (s_callbacks.on_bind_result != 0) {
        s_callbacks.on_bind_result(s_context, correlation_id, short_addr, result);
    }
}

void hal_zigbee_notify_configure_reporting_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
    if (s_callbacks.on_configure_reporting_result != 0) {
        s_callbacks.on_configure_reporting_result(s_context, correlation_id, short_addr, result);
    }
}

void hal_zigbee_notify_attribute_report_raw(const hal_zigbee_raw_attribute_report_t* report) {
    if (report == NULL) {
        return;
    }
    if (report->payload_len > 0U && report->payload == NULL) {
        return;
    }
    if (s_callbacks.on_attribute_report_raw != 0) {
        hal_zigbee_raw_attribute_report_t normalized = *report;
        if (!normalized.has_lqi) {
            normalized.lqi = 0U;
        }
        if (!normalized.has_rssi) {
            normalized.rssi_dbm = 0;
        }
        s_callbacks.on_attribute_report_raw(s_context, &normalized);
    }
}

void hal_zigbee_simulate_device_joined(uint16_t short_addr) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_device_joined(short_addr);
#else
    (void)short_addr;
#endif
}

void hal_zigbee_simulate_device_left(uint16_t short_addr) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_device_left(short_addr);
#else
    (void)short_addr;
#endif
}

void hal_zigbee_simulate_attribute_report(
    uint16_t short_addr,
    uint16_t cluster_id,
    uint16_t attribute_id,
    bool value_bool,
    uint32_t value_u32) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_attribute_report(short_addr, cluster_id, attribute_id, value_bool, value_u32);
#else
    (void)short_addr;
    (void)cluster_id;
    (void)attribute_id;
    (void)value_bool;
    (void)value_u32;
#endif
}

void hal_zigbee_simulate_command_result(uint32_t correlation_id, hal_zigbee_result_t result) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_command_result(correlation_id, result);
#else
    (void)correlation_id;
    (void)result;
#endif
}

void hal_zigbee_simulate_interview_completed(uint32_t correlation_id, uint16_t short_addr) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_interview_result(correlation_id, short_addr, HAL_ZIGBEE_RESULT_SUCCESS);
#else
    (void)correlation_id;
    (void)short_addr;
#endif
}

void hal_zigbee_simulate_bind_result(uint32_t correlation_id, uint16_t short_addr, hal_zigbee_result_t result) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_bind_result(correlation_id, short_addr, result);
#else
    (void)correlation_id;
    (void)short_addr;
    (void)result;
#endif
}

void hal_zigbee_simulate_reporting_config_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
#ifndef ESP_PLATFORM
    // TEMP MOCK PATH (!ESP_PLATFORM): explicit simulation helper for host tests.
    hal_zigbee_notify_configure_reporting_result(correlation_id, short_addr, result);
#else
    (void)correlation_id;
    (void)short_addr;
    (void)result;
#endif
}
