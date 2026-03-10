/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(SERVICE_RUNTIME_TEST_HOOKS)
#error "hal_zigbee_test.h is test-only and requires SERVICE_RUNTIME_TEST_HOOKS"
#endif

#include <stdbool.h>
#include <stdint.h>

#include "hal_zigbee.h"

#ifdef __cplusplus
extern "C" {
#endif

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
void hal_zigbee_simulate_network_formed(bool formed);
void hal_zigbee_simulate_start_network_formation_status_once(hal_zigbee_status_t status);

void hal_zigbee_test_apply_permit_join_status(uint8_t duration_seconds);

#ifdef __cplusplus
}
#endif
