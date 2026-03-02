/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <stdbool.h>
#include <stdint.h>

#include "hal_zigbee.h"
#include "unity.h"

typedef struct {
    bool joined_called;
    bool left_called;
    bool report_called;
    bool result_called;
    uint16_t short_addr;
    uint16_t cluster_id;
    uint16_t attribute_id;
    bool value_bool;
    uint32_t value_u32;
    uint32_t correlation_id;
    hal_zigbee_result_t result;
} zigbee_callback_capture_t;

static void on_device_joined(void* context, uint16_t short_addr) {
    zigbee_callback_capture_t* capture = (zigbee_callback_capture_t*)context;
    capture->joined_called = true;
    capture->short_addr = short_addr;
}

static void on_device_left(void* context, uint16_t short_addr) {
    zigbee_callback_capture_t* capture = (zigbee_callback_capture_t*)context;
    capture->left_called = true;
    capture->short_addr = short_addr;
}

static void on_attribute_report(
    void* context,
    uint16_t short_addr,
    uint16_t cluster_id,
    uint16_t attribute_id,
    bool value_bool,
    uint32_t value_u32) {
    zigbee_callback_capture_t* capture = (zigbee_callback_capture_t*)context;
    capture->report_called = true;
    capture->short_addr = short_addr;
    capture->cluster_id = cluster_id;
    capture->attribute_id = attribute_id;
    capture->value_bool = value_bool;
    capture->value_u32 = value_u32;
}

static void on_command_result(void* context, uint32_t correlation_id, hal_zigbee_result_t result) {
    zigbee_callback_capture_t* capture = (zigbee_callback_capture_t*)context;
    capture->result_called = true;
    capture->correlation_id = correlation_id;
    capture->result = result;
}

static bool is_valid_request_status(hal_zigbee_status_t status) {
    return status == HAL_ZIGBEE_STATUS_OK || status == HAL_ZIGBEE_STATUS_NOT_STARTED ||
           status == HAL_ZIGBEE_STATUS_NOT_LINKED;
}

void test_hal_zigbee_init_contract(void) {
    const hal_zigbee_status_t init_status = hal_zigbee_init();
    TEST_ASSERT_TRUE(
        init_status == HAL_ZIGBEE_STATUS_OK || init_status == HAL_ZIGBEE_STATUS_NOT_LINKED);
}

void test_hal_zigbee_notifies_registered_callbacks(void) {
    zigbee_callback_capture_t capture = {0};

    const hal_zigbee_status_t init_status = hal_zigbee_init();
    if (init_status == HAL_ZIGBEE_STATUS_NOT_LINKED) {
        TEST_IGNORE_MESSAGE("Real Zigbee adapter is not linked in this target test build");
    }
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, init_status);

    hal_zigbee_callbacks_t callbacks = {
        .on_device_joined = on_device_joined,
        .on_device_left = on_device_left,
        .on_attribute_report = on_attribute_report,
        .on_command_result = on_command_result,
    };
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, hal_zigbee_register_callbacks(&callbacks, &capture));

    hal_zigbee_notify_device_joined(0x2211);
    TEST_ASSERT_TRUE(capture.joined_called);
    TEST_ASSERT_EQUAL_HEX16(0x2211, capture.short_addr);

    hal_zigbee_notify_attribute_report(0x2211, 0x0006, 0x0000, true, 1);
    TEST_ASSERT_TRUE(capture.report_called);
    TEST_ASSERT_EQUAL_HEX16(0x2211, capture.short_addr);
    TEST_ASSERT_EQUAL_HEX16(0x0006, capture.cluster_id);
    TEST_ASSERT_EQUAL_HEX16(0x0000, capture.attribute_id);
    TEST_ASSERT_TRUE(capture.value_bool);
    TEST_ASSERT_EQUAL_UINT32(1, capture.value_u32);

    hal_zigbee_notify_command_result(41, HAL_ZIGBEE_RESULT_TIMEOUT);
    TEST_ASSERT_TRUE(capture.result_called);
    TEST_ASSERT_EQUAL_UINT32(41, capture.correlation_id);
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_RESULT_TIMEOUT, capture.result);

    hal_zigbee_notify_device_left(0x2211);
    TEST_ASSERT_TRUE(capture.left_called);
    TEST_ASSERT_EQUAL_HEX16(0x2211, capture.short_addr);
}

void test_hal_zigbee_rejects_null_callbacks(void) {
    const hal_zigbee_status_t init_status = hal_zigbee_init();
    if (init_status == HAL_ZIGBEE_STATUS_NOT_LINKED) {
        TEST_IGNORE_MESSAGE("Real Zigbee adapter is not linked in this target test build");
    }
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, init_status);
    TEST_ASSERT_NOT_EQUAL(HAL_ZIGBEE_STATUS_OK, hal_zigbee_register_callbacks(0, 0));
}

void test_hal_zigbee_request_api_contract(void) {
    const hal_zigbee_status_t init_status = hal_zigbee_init();
    if (init_status == HAL_ZIGBEE_STATUS_NOT_LINKED) {
        TEST_IGNORE_MESSAGE("Real Zigbee adapter is not linked in this target test build");
    }
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, init_status);

    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_INVALID_ARG, hal_zigbee_request_interview(0, 0x2201));
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_INVALID_ARG, hal_zigbee_request_interview(1, 0xFFFF));

    TEST_ASSERT_EQUAL_INT(
        HAL_ZIGBEE_STATUS_INVALID_ARG, hal_zigbee_request_bind(0, 0x2201, 1, 0x0006, 1));
    TEST_ASSERT_EQUAL_INT(
        HAL_ZIGBEE_STATUS_INVALID_ARG, hal_zigbee_request_bind(1, 0x2201, 0, 0x0006, 1));

    TEST_ASSERT_EQUAL_INT(
        HAL_ZIGBEE_STATUS_INVALID_ARG,
        hal_zigbee_request_configure_reporting(1, 0x2201, 1, 0x0402, 0x0000, 30, 10, 1));
    TEST_ASSERT_EQUAL_INT(
        HAL_ZIGBEE_STATUS_INVALID_ARG,
        hal_zigbee_request_configure_reporting(1, 0x2201, 0, 0x0402, 0x0000, 5, 10, 1));

    TEST_ASSERT_EQUAL_INT(
        HAL_ZIGBEE_STATUS_INVALID_ARG,
        hal_zigbee_request_read_attribute(1, 0x2201, 0, 0x0402, 0x0000));

    TEST_ASSERT_TRUE(is_valid_request_status(hal_zigbee_request_interview(1, 0x2201)));
    TEST_ASSERT_TRUE(is_valid_request_status(hal_zigbee_request_bind(2, 0x2201, 1, 0x0006, 1)));
    TEST_ASSERT_TRUE(is_valid_request_status(
        hal_zigbee_request_configure_reporting(3, 0x2201, 1, 0x0402, 0x0000, 5, 300, 10)));
    TEST_ASSERT_TRUE(is_valid_request_status(hal_zigbee_request_read_attribute(4, 0x2201, 1, 0x0402, 0x0000)));
}
