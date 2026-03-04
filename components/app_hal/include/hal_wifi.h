/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool is_open;
} hal_wifi_scan_record_t;

typedef struct {
    void (*on_network_up)(void* context);
    void (*on_network_down)(void* context);
} hal_wifi_callbacks_t;

typedef enum {
    HAL_WIFI_MODE_NULL = 0,
    HAL_WIFI_MODE_STA = 1,
    HAL_WIFI_MODE_AP = 2,
    HAL_WIFI_MODE_APSTA = 3,
} hal_wifi_mode_t;

typedef enum {
    HAL_WIFI_STATUS_OK = 0,
    HAL_WIFI_STATUS_INVALID_ARG = -1,
    HAL_WIFI_STATUS_ERR = -2,
    HAL_WIFI_STATUS_BAD_STATE = -3,
    HAL_WIFI_STATUS_TIMEOUT = -4,
} hal_wifi_status_t;

hal_wifi_status_t hal_wifi_init(void);
hal_wifi_status_t hal_wifi_register_callbacks(const hal_wifi_callbacks_t* callbacks, void* context);
hal_wifi_status_t hal_wifi_get_mode(hal_wifi_mode_t* mode);
hal_wifi_status_t hal_wifi_set_mode(hal_wifi_mode_t mode);
hal_wifi_status_t hal_wifi_start_ap(const char* ssid, const char* password);
hal_wifi_status_t hal_wifi_connect_sta_async(const char* ssid, const char* password);
hal_wifi_status_t hal_wifi_connect_sta(const char* ssid, const char* password);
hal_wifi_status_t hal_wifi_get_ap_client_count(size_t* count);
hal_wifi_status_t hal_wifi_scan(hal_wifi_scan_record_t* records, size_t capacity, size_t* found_count);
hal_wifi_status_t hal_wifi_refresh(void);

void hal_wifi_simulate_network_up(void);
void hal_wifi_simulate_network_down(void);
void hal_wifi_simulate_connect_failure(bool enable);

#ifdef __cplusplus
}
#endif
