/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_ZIGBEE_RESULT_SUCCESS = 0,
    HAL_ZIGBEE_RESULT_TIMEOUT = 1,
    HAL_ZIGBEE_RESULT_FAILED = 2,
} hal_zigbee_result_t;

typedef enum {
    HAL_ZIGBEE_STATUS_OK = 0,
    HAL_ZIGBEE_STATUS_ERR = -1,
    HAL_ZIGBEE_STATUS_NETWORK_NOT_FORMED = -2,
    HAL_ZIGBEE_STATUS_INVALID_ARG = -3,
    HAL_ZIGBEE_STATUS_NOT_STARTED = -4,
    HAL_ZIGBEE_STATUS_NOT_LINKED = -5,
} hal_zigbee_status_t;

typedef struct {
    uint16_t short_addr;
    uint8_t endpoint;
    uint16_t cluster_id;
    uint16_t attribute_id;
    uint8_t zcl_data_type;
    bool has_lqi;
    uint8_t lqi;
    bool has_rssi;
    int8_t rssi_dbm;
    const uint8_t* payload;
    uint8_t payload_len;
} hal_zigbee_raw_attribute_report_t;

typedef struct {
    void (*on_device_joined)(void* context, uint16_t short_addr);
    void (*on_device_left)(void* context, uint16_t short_addr);
    void (*on_attribute_report)(
        void* context,
        uint16_t short_addr,
        uint16_t cluster_id,
        uint16_t attribute_id,
        bool value_bool,
        uint32_t value_u32);
    void (*on_attribute_report_raw)(void* context, const hal_zigbee_raw_attribute_report_t* report);
    void (*on_command_result)(void* context, uint32_t correlation_id, hal_zigbee_result_t result);
    void (*on_interview_result)(
        void* context,
        uint32_t correlation_id,
        uint16_t short_addr,
        hal_zigbee_result_t result);
    void (*on_bind_result)(
        void* context,
        uint32_t correlation_id,
        uint16_t short_addr,
        hal_zigbee_result_t result);
    void (*on_configure_reporting_result)(
        void* context,
        uint32_t correlation_id,
        uint16_t short_addr,
        hal_zigbee_result_t result);
} hal_zigbee_callbacks_t;

// Target path contract:
// On ESP target builds (ESP_PLATFORM), these APIs must be backed by a real
// Zigbee stack adapter implementation.
//
// Host/test path contract:
// Under !ESP_PLATFORM this module intentionally uses a temporary mock path
// to keep host tests deterministic and independent from ESP Zigbee runtime.

hal_zigbee_status_t hal_zigbee_init(void);
hal_zigbee_status_t hal_zigbee_set_primary_channel_mask(uint32_t channel_mask);
hal_zigbee_status_t hal_zigbee_set_max_children(uint8_t max_children);
hal_zigbee_status_t hal_zigbee_register_callbacks(const hal_zigbee_callbacks_t* callbacks, void* context);
hal_zigbee_status_t hal_zigbee_send_on_off(uint32_t correlation_id, uint16_t short_addr, bool on);
hal_zigbee_status_t hal_zigbee_request_interview(uint32_t correlation_id, uint16_t short_addr);
hal_zigbee_status_t hal_zigbee_request_bind(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t src_endpoint,
    uint16_t cluster_id,
    uint8_t dst_endpoint);
hal_zigbee_status_t hal_zigbee_request_configure_reporting(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint16_t min_interval_seconds,
    uint16_t max_interval_seconds,
    uint32_t reportable_change);
hal_zigbee_status_t hal_zigbee_request_read_attribute(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id);
hal_zigbee_status_t hal_zigbee_start_network_formation(void);
hal_zigbee_status_t hal_zigbee_open_network(uint16_t duration_seconds);
hal_zigbee_status_t hal_zigbee_close_network(void);
hal_zigbee_status_t hal_zigbee_remove_device(uint16_t short_addr);
hal_zigbee_status_t hal_zigbee_get_join_window_status(bool* open, uint16_t* seconds_left);
bool hal_zigbee_is_network_formed(void);
void hal_zigbee_poll(void);

void hal_zigbee_notify_device_joined(uint16_t short_addr);
void hal_zigbee_notify_device_left(uint16_t short_addr);
void hal_zigbee_notify_attribute_report(
    uint16_t short_addr,
    uint16_t cluster_id,
    uint16_t attribute_id,
    bool value_bool,
    uint32_t value_u32);
void hal_zigbee_notify_command_result(uint32_t correlation_id, hal_zigbee_result_t result);
void hal_zigbee_notify_interview_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result);
void hal_zigbee_notify_bind_result(uint32_t correlation_id, uint16_t short_addr, hal_zigbee_result_t result);
void hal_zigbee_notify_configure_reporting_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result);
void hal_zigbee_notify_attribute_report_raw(const hal_zigbee_raw_attribute_report_t* report);

void hal_zigbee_simulate_device_joined(uint16_t short_addr);
void hal_zigbee_simulate_device_left(uint16_t short_addr);
void hal_zigbee_simulate_attribute_report(
    uint16_t short_addr,
    uint16_t cluster_id,
    uint16_t attribute_id,
    bool value_bool,
    uint32_t value_u32);
void hal_zigbee_simulate_command_result(uint32_t correlation_id, hal_zigbee_result_t result);
void hal_zigbee_simulate_interview_completed(uint32_t correlation_id, uint16_t short_addr);
void hal_zigbee_simulate_bind_result(uint32_t correlation_id, uint16_t short_addr, hal_zigbee_result_t result);
void hal_zigbee_simulate_reporting_config_result(
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result);

#ifdef __cplusplus
}
#endif
