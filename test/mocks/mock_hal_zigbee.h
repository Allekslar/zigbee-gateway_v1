/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t interview_calls;
    uint32_t bind_calls;
    uint32_t configure_reporting_calls;
    uint32_t read_attribute_calls;
    uint32_t last_correlation_id;
    uint16_t last_short_addr;
    uint8_t last_src_endpoint;
    uint16_t last_cluster_id;
    uint8_t last_dst_endpoint;
    uint8_t last_endpoint;
    uint16_t last_attribute_id;
    uint16_t last_min_interval_seconds;
    uint16_t last_max_interval_seconds;
    uint32_t last_reportable_change;
} mock_hal_zigbee_stats_t;

int mock_hal_zigbee_reset(void);
void mock_hal_zigbee_note_interview(uint32_t correlation_id, uint16_t short_addr);
void mock_hal_zigbee_note_bind(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t src_endpoint,
    uint16_t cluster_id,
    uint8_t dst_endpoint);
void mock_hal_zigbee_note_configure_reporting(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint16_t min_interval_seconds,
    uint16_t max_interval_seconds,
    uint32_t reportable_change);
void mock_hal_zigbee_note_read_attribute(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id);
mock_hal_zigbee_stats_t mock_hal_zigbee_stats(void);
