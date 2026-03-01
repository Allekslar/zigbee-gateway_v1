/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_wifi.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "log_tags.h"
#endif

static hal_wifi_callbacks_t s_callbacks;
static void* s_context = 0;
#ifndef ESP_PLATFORM
static hal_wifi_mode_t s_mock_mode = HAL_WIFI_MODE_NULL;
static bool s_simulate_connect_failure = false;
#endif

#ifdef ESP_PLATFORM
static const char* kTag = LOG_TAG_HAL_WIFI;
static bool s_wifi_initialized = false;
static bool s_event_handlers_registered = false;
static esp_netif_t* s_sta_netif = 0;
static esp_netif_t* s_ap_netif = 0;
static wifi_mode_t s_last_mode = WIFI_MODE_NULL;
static EventGroupHandle_t s_sta_event_group = NULL;
static SemaphoreHandle_t s_wifi_ops_mutex = NULL;

static const EventBits_t kStaGotIpBit = (1U << 0);
static const EventBits_t kStaDisconnectedBit = (1U << 1);
static const TickType_t kStaConnectTimeoutTicks = pdMS_TO_TICKS(12000);
static const TickType_t kWifiOpsLockTimeoutTicks = pdMS_TO_TICKS(15000);
#define HAL_WIFI_SCAN_BUFFER_CAPACITY 16U

static int to_esp_wifi_mode(hal_wifi_mode_t mode, wifi_mode_t* out) {
    if (out == NULL) {
        return -1;
    }

    switch (mode) {
        case HAL_WIFI_MODE_NULL:
            *out = WIFI_MODE_NULL;
            return 0;
        case HAL_WIFI_MODE_STA:
            *out = WIFI_MODE_STA;
            return 0;
        case HAL_WIFI_MODE_AP:
            *out = WIFI_MODE_AP;
            return 0;
        case HAL_WIFI_MODE_APSTA:
            *out = WIFI_MODE_APSTA;
            return 0;
        default:
            return -1;
    }
}

static hal_wifi_mode_t from_esp_wifi_mode(wifi_mode_t mode) {
    switch (mode) {
        case WIFI_MODE_STA:
            return HAL_WIFI_MODE_STA;
        case WIFI_MODE_AP:
            return HAL_WIFI_MODE_AP;
        case WIFI_MODE_APSTA:
            return HAL_WIFI_MODE_APSTA;
        case WIFI_MODE_NULL:
        default:
            return HAL_WIFI_MODE_NULL;
    }
}

static void notify_network_up(void) {
    if (s_callbacks.on_network_up != 0) {
        s_callbacks.on_network_up(s_context);
    }
}

static void notify_network_down(void) {
    if (s_callbacks.on_network_down != 0) {
        s_callbacks.on_network_down(s_context);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_DISCONNECTED: {
                if (s_sta_event_group != NULL) {
                    xEventGroupSetBits(s_sta_event_group, kStaDisconnectedBit);
                }
                if (event_data != NULL) {
                    const wifi_event_sta_disconnected_t* disconn =
                        (const wifi_event_sta_disconnected_t*)event_data;
                    ESP_LOGW(kTag, "STA disconnected, reason=%u", (unsigned)disconn->reason);
                } else {
                    ESP_LOGW(kTag, "STA disconnected");
                }
                notify_network_down();
                break;
            }
            case WIFI_EVENT_AP_STOP:
                notify_network_down();
                break;
            default:
                break;
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_sta_event_group != NULL) {
            xEventGroupSetBits(s_sta_event_group, kStaGotIpBit);
        }
        notify_network_up();
    }
}

static hal_wifi_status_t ensure_wifi_stack_ready(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return HAL_WIFI_STATUS_ERR;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return HAL_WIFI_STATUS_ERR;
    }

    if (s_sta_netif == 0) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (s_ap_netif == 0) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (s_sta_netif == 0 || s_ap_netif == 0) {
        return HAL_WIFI_STATUS_ERR;
    }

    if (s_sta_event_group == NULL) {
        s_sta_event_group = xEventGroupCreate();
        if (s_sta_event_group == NULL) {
            return HAL_WIFI_STATUS_ERR;
        }
    }

    if (!s_wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            return HAL_WIFI_STATUS_ERR;
        }
        s_wifi_initialized = true;
    }

    if (!s_event_handlers_registered) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, 0);
        if (err != ESP_OK) {
            return HAL_WIFI_STATUS_ERR;
        }

        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, 0);
        if (err != ESP_OK) {
            return HAL_WIFI_STATUS_ERR;
        }

        s_event_handlers_registered = true;
    }

    return HAL_WIFI_STATUS_OK;
}

static hal_wifi_status_t ensure_wifi_ops_lock_ready(void) {
    if (s_wifi_ops_mutex == NULL) {
        s_wifi_ops_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_wifi_ops_mutex == NULL) {
            return HAL_WIFI_STATUS_ERR;
        }
    }
    return HAL_WIFI_STATUS_OK;
}

static hal_wifi_status_t wifi_ops_lock(void) {
    if (ensure_wifi_ops_lock_ready() != HAL_WIFI_STATUS_OK) {
        return HAL_WIFI_STATUS_ERR;
    }
    if (xSemaphoreTakeRecursive(s_wifi_ops_mutex, kWifiOpsLockTimeoutTicks) != pdTRUE) {
        ESP_LOGW(kTag, "Wi-Fi ops lock timeout");
        return HAL_WIFI_STATUS_TIMEOUT;
    }
    return HAL_WIFI_STATUS_OK;
}

static void wifi_ops_unlock(void) {
    if (s_wifi_ops_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(s_wifi_ops_mutex);
    }
}
#endif

hal_wifi_status_t hal_wifi_init(void) {
    s_callbacks = (hal_wifi_callbacks_t){0};
    s_context = 0;

#ifdef ESP_PLATFORM
    s_last_mode = WIFI_MODE_NULL;
    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        return HAL_WIFI_STATUS_ERR;
    }
    return ensure_wifi_ops_lock_ready();
#else
    s_mock_mode = HAL_WIFI_MODE_NULL;
    return HAL_WIFI_STATUS_OK;
#endif
}

hal_wifi_status_t hal_wifi_register_callbacks(const hal_wifi_callbacks_t* callbacks, void* context) {
    if (callbacks == 0) {
        return HAL_WIFI_STATUS_INVALID_ARG;
    }

    s_callbacks = *callbacks;
    s_context = context;
    return HAL_WIFI_STATUS_OK;
}

hal_wifi_status_t hal_wifi_get_mode(hal_wifi_mode_t* mode) {
    if (mode == 0) {
        return HAL_WIFI_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        return HAL_WIFI_STATUS_ERR;
    }

    wifi_mode_t esp_mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&esp_mode) != ESP_OK) {
        return HAL_WIFI_STATUS_ERR;
    }

    *mode = from_esp_wifi_mode(esp_mode);
    return HAL_WIFI_STATUS_OK;
#else
    *mode = s_mock_mode;
    return HAL_WIFI_STATUS_OK;
#endif
}

hal_wifi_status_t hal_wifi_set_mode(hal_wifi_mode_t mode) {
#ifdef ESP_PLATFORM
    const hal_wifi_status_t lock_status = wifi_ops_lock();
    if (lock_status != HAL_WIFI_STATUS_OK) {
        return lock_status;
    }

    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    wifi_mode_t target_mode = WIFI_MODE_NULL;
    if (to_esp_wifi_mode(mode, &target_mode) != 0) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_INVALID_ARG;
    }

    wifi_mode_t current_mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&current_mode) != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    if (current_mode == target_mode) {
        s_last_mode = current_mode;
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_OK;
    }

    (void)esp_wifi_stop();
    if (esp_wifi_set_mode(target_mode) != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    if (target_mode != WIFI_MODE_NULL) {
        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN && start_err != ESP_ERR_WIFI_NOT_STOPPED) {
            wifi_ops_unlock();
            return HAL_WIFI_STATUS_ERR;
        }
        (void)esp_wifi_set_ps(WIFI_PS_NONE);
    }

    s_last_mode = target_mode;
    wifi_ops_unlock();
    return HAL_WIFI_STATUS_OK;
#else
    s_mock_mode = mode;
    return HAL_WIFI_STATUS_OK;
#endif
}

hal_wifi_status_t hal_wifi_start_ap(const char* ssid, const char* password) {
#ifdef ESP_PLATFORM
    const hal_wifi_status_t lock_status = wifi_ops_lock();
    if (lock_status != HAL_WIFI_STATUS_OK) {
        return lock_status;
    }

    if (ssid == 0 || ssid[0] == '\0') {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_INVALID_ARG;
    }

    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    wifi_config_t config = {0};
    const size_t ssid_len = strnlen(ssid, sizeof(config.ap.ssid));
    memcpy(config.ap.ssid, ssid, ssid_len);
    config.ap.ssid_len = ssid_len;

    if (password != 0 && password[0] != '\0') {
        const size_t pass_len = strnlen(password, sizeof(config.ap.password));
        memcpy(config.ap.password, password, pass_len);
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        config.ap.authmode = WIFI_AUTH_OPEN;
    }

    config.ap.channel = 1;
    config.ap.max_connection = 4;

    (void)esp_wifi_stop();
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }
    if (esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }
    if (esp_wifi_start() != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);

    s_last_mode = WIFI_MODE_AP;
    wifi_ops_unlock();
    return HAL_WIFI_STATUS_OK;
#else
    (void)ssid;
    (void)password;
    s_mock_mode = HAL_WIFI_MODE_AP;
    if (s_callbacks.on_network_up != 0) {
        s_callbacks.on_network_up(s_context);
    }
    return HAL_WIFI_STATUS_OK;
#endif
}

hal_wifi_status_t hal_wifi_connect_sta_async(const char* ssid, const char* password) {
#ifdef ESP_PLATFORM
    const hal_wifi_status_t lock_status = wifi_ops_lock();
    if (lock_status != HAL_WIFI_STATUS_OK) {
        return lock_status;
    }

    if (ssid == 0 || ssid[0] == '\0') {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_INVALID_ARG;
    }

    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    wifi_config_t config = {0};
    const size_t ssid_len = strnlen(ssid, sizeof(config.sta.ssid));
    memcpy(config.sta.ssid, ssid, ssid_len);

    if (password != 0 && password[0] != '\0') {
        const size_t pass_len = strnlen(password, sizeof(config.sta.password));
        memcpy(config.sta.password, password, pass_len);
    }

    wifi_mode_t current_mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&current_mode) != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN && start_err != ESP_ERR_WIFI_NOT_STOPPED) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    if (s_sta_event_group != NULL) {
        (void)xEventGroupClearBits(s_sta_event_group, kStaGotIpBit | kStaDisconnectedBit);
    }

    if (esp_wifi_connect() != ESP_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    s_last_mode = current_mode;
    wifi_ops_unlock();
    return HAL_WIFI_STATUS_OK;
#else
    (void)ssid;
    (void)password;
    if (s_simulate_connect_failure) {
        return HAL_WIFI_STATUS_ERR;
    }
    if (s_mock_mode != HAL_WIFI_MODE_STA && s_mock_mode != HAL_WIFI_MODE_APSTA) {
        return HAL_WIFI_STATUS_BAD_STATE;
    }
    if (s_callbacks.on_network_up != 0) {
        s_callbacks.on_network_up(s_context);
    }
    return HAL_WIFI_STATUS_OK;
#endif
}

hal_wifi_status_t hal_wifi_connect_sta(const char* ssid, const char* password) {
#ifdef ESP_PLATFORM
    const hal_wifi_status_t lock_status = wifi_ops_lock();
    if (lock_status != HAL_WIFI_STATUS_OK) {
        return lock_status;
    }

    const hal_wifi_status_t async_status = hal_wifi_connect_sta_async(ssid, password);
    if (async_status != HAL_WIFI_STATUS_OK) {
        wifi_ops_unlock();
        return async_status;
    }

    EventBits_t bits = 0;
    if (s_sta_event_group != NULL) {
        bits = xEventGroupWaitBits(
            s_sta_event_group,
            kStaGotIpBit | kStaDisconnectedBit,
            pdTRUE,
            pdFALSE,
            kStaConnectTimeoutTicks);
    }

    if ((bits & kStaGotIpBit) == 0U) {
        ESP_LOGW(kTag, "STA connect timeout/fail");
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_TIMEOUT;
    }

    wifi_ops_unlock();
    return HAL_WIFI_STATUS_OK;
#else
    return hal_wifi_connect_sta_async(ssid, password);
#endif
}

hal_wifi_status_t hal_wifi_scan(hal_wifi_scan_record_t* records, size_t capacity, size_t* found_count) {
    if (records == 0 || found_count == 0 || capacity == 0) {
        return HAL_WIFI_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    const hal_wifi_status_t lock_status = wifi_ops_lock();
    if (lock_status != HAL_WIFI_STATUS_OK) {
        return lock_status;
    }

    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "scan:get_mode failed: %s", esp_err_to_name(err));
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }
    ESP_LOGI(kTag, "scan:start mode=%d capacity=%u", (int)mode, (unsigned)capacity);

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGE(kTag, "scan:ensure_start failed: %s", esp_err_to_name(err));
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    s_last_mode = mode;

    wifi_scan_config_t scan_config = {0};
    scan_config.show_hidden = true;
    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "scan:start_scan failed: %s", esp_err_to_name(err));
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "scan:get_ap_num failed: %s", esp_err_to_name(err));
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    uint16_t to_fetch = ap_count;
    if (capacity < (size_t)to_fetch) {
        to_fetch = (uint16_t)capacity;
    }

    if (to_fetch > HAL_WIFI_SCAN_BUFFER_CAPACITY) {
        ESP_LOGW(
            kTag,
            "scan:truncate result count from %u to bounded capacity %u",
            (unsigned)to_fetch,
            (unsigned)HAL_WIFI_SCAN_BUFFER_CAPACITY);
        to_fetch = HAL_WIFI_SCAN_BUFFER_CAPACITY;
    }

    if (to_fetch > 0) {
        wifi_ap_record_t ap_records[HAL_WIFI_SCAN_BUFFER_CAPACITY];
        memset(ap_records, 0, sizeof(ap_records));

        uint16_t fetch_count = to_fetch;
        err = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "scan:get_ap_records failed: %s", esp_err_to_name(err));
            wifi_ops_unlock();
            return HAL_WIFI_STATUS_ERR;
        }

        for (uint16_t i = 0; i < fetch_count; ++i) {
            hal_wifi_scan_record_t* record = &records[i];
            memset(record, 0, sizeof(*record));
            memcpy(record->ssid, ap_records[i].ssid, sizeof(record->ssid) - 1);
            record->rssi = (int8_t)ap_records[i].rssi;
            record->is_open = ap_records[i].authmode == WIFI_AUTH_OPEN;
        }
        to_fetch = fetch_count;
    }

    *found_count = to_fetch;
    ESP_LOGI(kTag, "scan:done found=%u", (unsigned)to_fetch);
    wifi_ops_unlock();
    return HAL_WIFI_STATUS_OK;
#else
    const char* sample_ssids[] = {"HomeWiFi", "OfficeWiFi", "Guest"};
    const int8_t sample_rssi[] = {-45, -62, -70};
    const bool sample_open[] = {false, false, true};
    const size_t sample_count = sizeof(sample_ssids) / sizeof(sample_ssids[0]);
    const size_t out_count = sample_count < capacity ? sample_count : capacity;

    for (size_t i = 0; i < out_count; ++i) {
        memset(&records[i], 0, sizeof(records[i]));
        strncpy(records[i].ssid, sample_ssids[i], sizeof(records[i].ssid) - 1);
        records[i].rssi = sample_rssi[i];
        records[i].is_open = sample_open[i];
    }

    *found_count = out_count;
    return HAL_WIFI_STATUS_OK;
#endif
}

hal_wifi_status_t hal_wifi_refresh(void) {
#ifdef ESP_PLATFORM
    const hal_wifi_status_t lock_status = wifi_ops_lock();
    if (lock_status != HAL_WIFI_STATUS_OK) {
        return lock_status;
    }

    if (ensure_wifi_stack_ready() != HAL_WIFI_STATUS_OK) {
        wifi_ops_unlock();
        return HAL_WIFI_STATUS_ERR;
    }

    if (s_last_mode == WIFI_MODE_STA || s_last_mode == WIFI_MODE_APSTA) {
        (void)esp_wifi_disconnect();
        const hal_wifi_status_t status = esp_wifi_connect() == ESP_OK ? HAL_WIFI_STATUS_OK : HAL_WIFI_STATUS_ERR;
        wifi_ops_unlock();
        return status;
    }

    if (s_last_mode == WIFI_MODE_AP) {
        if (esp_wifi_stop() != ESP_OK) {
            wifi_ops_unlock();
            return HAL_WIFI_STATUS_ERR;
        }
        const hal_wifi_status_t status = esp_wifi_start() == ESP_OK ? HAL_WIFI_STATUS_OK : HAL_WIFI_STATUS_ERR;
        wifi_ops_unlock();
        return status;
    }

    wifi_ops_unlock();
    return HAL_WIFI_STATUS_BAD_STATE;
#else
    if (s_callbacks.on_network_down != 0) {
        s_callbacks.on_network_down(s_context);
    }
    if (s_callbacks.on_network_up != 0) {
        s_callbacks.on_network_up(s_context);
    }
    return HAL_WIFI_STATUS_OK;
#endif
}

void hal_wifi_simulate_network_up(void) {
#ifndef ESP_PLATFORM
    if (s_callbacks.on_network_up != 0) {
        s_callbacks.on_network_up(s_context);
    }
#endif
}

void hal_wifi_simulate_connect_failure(bool enable) {
#ifndef ESP_PLATFORM
    s_simulate_connect_failure = enable;
#endif
}

void hal_wifi_simulate_network_down(void) {
#ifndef ESP_PLATFORM
    if (s_callbacks.on_network_down != 0) {
        s_callbacks.on_network_down(s_context);
    }
#endif
}
