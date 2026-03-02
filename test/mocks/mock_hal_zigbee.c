/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "mock_hal_zigbee.h"

static mock_hal_zigbee_stats_t s_stats;

int mock_hal_zigbee_reset(void) {
    s_stats = (mock_hal_zigbee_stats_t){0};
    return 0;
}

void mock_hal_zigbee_note_interview(uint32_t correlation_id, uint16_t short_addr) {
    ++s_stats.interview_calls;
    s_stats.last_correlation_id = correlation_id;
    s_stats.last_short_addr = short_addr;
}

void mock_hal_zigbee_note_bind(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t src_endpoint,
    uint16_t cluster_id,
    uint8_t dst_endpoint) {
    ++s_stats.bind_calls;
    s_stats.last_correlation_id = correlation_id;
    s_stats.last_short_addr = short_addr;
    s_stats.last_src_endpoint = src_endpoint;
    s_stats.last_cluster_id = cluster_id;
    s_stats.last_dst_endpoint = dst_endpoint;
}

void mock_hal_zigbee_note_configure_reporting(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint16_t min_interval_seconds,
    uint16_t max_interval_seconds,
    uint32_t reportable_change) {
    ++s_stats.configure_reporting_calls;
    s_stats.last_correlation_id = correlation_id;
    s_stats.last_short_addr = short_addr;
    s_stats.last_endpoint = endpoint;
    s_stats.last_cluster_id = cluster_id;
    s_stats.last_attribute_id = attribute_id;
    s_stats.last_min_interval_seconds = min_interval_seconds;
    s_stats.last_max_interval_seconds = max_interval_seconds;
    s_stats.last_reportable_change = reportable_change;
}

void mock_hal_zigbee_note_read_attribute(
    uint32_t correlation_id,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id) {
    ++s_stats.read_attribute_calls;
    s_stats.last_correlation_id = correlation_id;
    s_stats.last_short_addr = short_addr;
    s_stats.last_endpoint = endpoint;
    s_stats.last_cluster_id = cluster_id;
    s_stats.last_attribute_id = attribute_id;
}

mock_hal_zigbee_stats_t mock_hal_zigbee_stats(void) {
    return s_stats;
}
