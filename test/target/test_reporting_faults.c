/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <stdbool.h>
#include <stdint.h>

#include "hal_zigbee.h"
#include "hal_zigbee_test.h"
#include "unity.h"

typedef struct {
    bool raw_called;
    uint32_t raw_count;
    bool configure_called;
    hal_zigbee_result_t configure_result;
    bool interview_called;
    bool bind_called;
    uint16_t short_addr;
} reporting_faults_capture_t;

static void on_attribute_report_raw(void* context, const hal_zigbee_raw_attribute_report_t* report) {
    reporting_faults_capture_t* capture = (reporting_faults_capture_t*)context;
    capture->raw_called = true;
    capture->raw_count += 1U;
    capture->short_addr = report->short_addr;
}

static void on_interview_result(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
    (void)correlation_id;
    (void)result;
    reporting_faults_capture_t* capture = (reporting_faults_capture_t*)context;
    capture->interview_called = true;
    capture->short_addr = short_addr;
}

static void on_bind_result(void* context, uint32_t correlation_id, uint16_t short_addr, hal_zigbee_result_t result) {
    (void)correlation_id;
    (void)result;
    reporting_faults_capture_t* capture = (reporting_faults_capture_t*)context;
    capture->bind_called = true;
    capture->short_addr = short_addr;
}

static void on_configure_reporting_result(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
    (void)correlation_id;
    reporting_faults_capture_t* capture = (reporting_faults_capture_t*)context;
    capture->configure_called = true;
    capture->configure_result = result;
    capture->short_addr = short_addr;
}

void test_reporting_faults_malformed_duplicate_out_of_order_timeout(void) {
    const hal_zigbee_status_t init_status = hal_zigbee_init();
    if (init_status == HAL_ZIGBEE_STATUS_NOT_LINKED) {
        TEST_IGNORE_MESSAGE("Real Zigbee adapter is not linked in this target test build");
    }
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, init_status);

    reporting_faults_capture_t capture = {0};
    const hal_zigbee_callbacks_t callbacks = {
        .on_device_joined = 0,
        .on_device_left = 0,
        .on_attribute_report = 0,
        .on_attribute_report_raw = on_attribute_report_raw,
        .on_command_result = 0,
        .on_interview_result = on_interview_result,
        .on_bind_result = on_bind_result,
        .on_configure_reporting_result = on_configure_reporting_result,
    };
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, hal_zigbee_register_callbacks(&callbacks, &capture));

    // Malformed raw payload: payload_len > 0 with payload == NULL must be ignored.
    hal_zigbee_raw_attribute_report_t malformed = {
        .short_addr = 0x4401U,
        .endpoint = 1U,
        .cluster_id = 0x0402U,
        .attribute_id = 0x0000U,
        .zcl_data_type = 0x29U,
        .has_lqi = false,
        .lqi = 0U,
        .has_rssi = false,
        .rssi_dbm = 0,
        .payload = NULL,
        .payload_len = 2U,
    };
    hal_zigbee_notify_attribute_report_raw(&malformed);
    TEST_ASSERT_FALSE(capture.raw_called);
    TEST_ASSERT_EQUAL_UINT32(0U, capture.raw_count);

    // Out-of-order callbacks should still be dispatched safely.
    hal_zigbee_notify_configure_reporting_result(901U, 0x4401U, HAL_ZIGBEE_RESULT_SUCCESS);
    TEST_ASSERT_TRUE(capture.configure_called);
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_RESULT_SUCCESS, capture.configure_result);
    TEST_ASSERT_EQUAL_HEX16(0x4401U, capture.short_addr);

    hal_zigbee_notify_interview_result(902U, 0x4401U, HAL_ZIGBEE_RESULT_SUCCESS);
    TEST_ASSERT_TRUE(capture.interview_called);
    TEST_ASSERT_EQUAL_HEX16(0x4401U, capture.short_addr);

    hal_zigbee_notify_bind_result(903U, 0x4401U, HAL_ZIGBEE_RESULT_SUCCESS);
    TEST_ASSERT_TRUE(capture.bind_called);
    TEST_ASSERT_EQUAL_HEX16(0x4401U, capture.short_addr);

    // Duplicate raw report should not crash and should be delivered twice.
    const uint8_t payload[] = {0x66U, 0x08U};
    const hal_zigbee_raw_attribute_report_t valid = {
        .short_addr = 0x4401U,
        .endpoint = 1U,
        .cluster_id = 0x0402U,
        .attribute_id = 0x0000U,
        .zcl_data_type = 0x29U,
        .has_lqi = true,
        .lqi = 170U,
        .has_rssi = true,
        .rssi_dbm = -70,
        .payload = payload,
        .payload_len = 2U,
    };
    hal_zigbee_notify_attribute_report_raw(&valid);
    hal_zigbee_notify_attribute_report_raw(&valid);
    TEST_ASSERT_TRUE(capture.raw_called);
    TEST_ASSERT_EQUAL_UINT32(2U, capture.raw_count);

    // Timeout fault must be traceable in callback result.
    capture.configure_called = false;
    hal_zigbee_notify_configure_reporting_result(904U, 0x4401U, HAL_ZIGBEE_RESULT_TIMEOUT);
    TEST_ASSERT_TRUE(capture.configure_called);
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_RESULT_TIMEOUT, capture.configure_result);
}
