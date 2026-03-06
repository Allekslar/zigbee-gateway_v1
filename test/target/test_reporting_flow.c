/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <stdbool.h>
#include <stdint.h>

#include "hal_zigbee.h"
#include "unity.h"

typedef struct {
    bool joined_called;
    bool interview_called;
    bool bind_called;
    bool configure_called;
    bool raw_report_called;
    uint16_t short_addr;
    uint32_t correlation_id;
    hal_zigbee_result_t result;
    uint16_t cluster_id;
    uint16_t attribute_id;
    uint8_t payload_len;
    uint8_t payload_first_byte;
} reporting_flow_capture_t;

static void on_device_joined(void* context, uint16_t short_addr) {
    reporting_flow_capture_t* capture = (reporting_flow_capture_t*)context;
    capture->joined_called = true;
    capture->short_addr = short_addr;
}

static void on_interview_result(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
    reporting_flow_capture_t* capture = (reporting_flow_capture_t*)context;
    capture->interview_called = true;
    capture->correlation_id = correlation_id;
    capture->short_addr = short_addr;
    capture->result = result;
}

static void on_bind_result(void* context, uint32_t correlation_id, uint16_t short_addr, hal_zigbee_result_t result) {
    reporting_flow_capture_t* capture = (reporting_flow_capture_t*)context;
    capture->bind_called = true;
    capture->correlation_id = correlation_id;
    capture->short_addr = short_addr;
    capture->result = result;
}

static void on_configure_reporting_result(
    void* context,
    uint32_t correlation_id,
    uint16_t short_addr,
    hal_zigbee_result_t result) {
    reporting_flow_capture_t* capture = (reporting_flow_capture_t*)context;
    capture->configure_called = true;
    capture->correlation_id = correlation_id;
    capture->short_addr = short_addr;
    capture->result = result;
}

static void on_attribute_report_raw(void* context, const hal_zigbee_raw_attribute_report_t* report) {
    reporting_flow_capture_t* capture = (reporting_flow_capture_t*)context;
    capture->raw_report_called = true;
    capture->short_addr = report->short_addr;
    capture->cluster_id = report->cluster_id;
    capture->attribute_id = report->attribute_id;
    capture->payload_len = report->payload_len;
    capture->payload_first_byte = (report->payload_len > 0U) ? report->payload[0] : 0U;
}

void test_reporting_flow_join_to_first_report_and_reboot_recovery(void) {
    const uint16_t short_addr = 0x3301U;
    const uint32_t interview_corr = 101U;
    const uint32_t bind_corr = 102U;
    const uint32_t config_corr = 103U;
    const uint8_t first_payload[] = {0x66U, 0x08U};  // 21.50 C in 0.01C format
    const uint8_t reboot_payload[] = {0x6AU, 0x08U}; // 21.54 C in 0.01C format

    const hal_zigbee_status_t init_status = hal_zigbee_init();
    if (init_status == HAL_ZIGBEE_STATUS_NOT_LINKED) {
        TEST_IGNORE_MESSAGE("Real Zigbee adapter is not linked in this target test build");
    }
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, init_status);

    reporting_flow_capture_t first = {0};
    const hal_zigbee_callbacks_t callbacks = {
        .on_device_joined = on_device_joined,
        .on_device_left = 0,
        .on_attribute_report = 0,
        .on_attribute_report_raw = on_attribute_report_raw,
        .on_command_result = 0,
        .on_interview_result = on_interview_result,
        .on_bind_result = on_bind_result,
        .on_configure_reporting_result = on_configure_reporting_result,
    };
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, hal_zigbee_register_callbacks(&callbacks, &first));

    // Lifecycle: join -> interview -> bind -> configure -> first report.
    hal_zigbee_notify_device_joined(short_addr);
    hal_zigbee_notify_interview_result(interview_corr, short_addr, HAL_ZIGBEE_RESULT_SUCCESS);
    hal_zigbee_notify_bind_result(bind_corr, short_addr, HAL_ZIGBEE_RESULT_SUCCESS);
    hal_zigbee_notify_configure_reporting_result(config_corr, short_addr, HAL_ZIGBEE_RESULT_SUCCESS);
    const hal_zigbee_raw_attribute_report_t first_report = {
        .short_addr = short_addr,
        .endpoint = 1U,
        .cluster_id = 0x0402U,
        .attribute_id = 0x0000U,
        .zcl_data_type = 0x29U,
        .has_lqi = true,
        .lqi = 187U,
        .has_rssi = true,
        .rssi_dbm = -67,
        .payload = first_payload,
        .payload_len = sizeof(first_payload),
    };
    hal_zigbee_notify_attribute_report_raw(&first_report);

    TEST_ASSERT_TRUE(first.joined_called);
    TEST_ASSERT_TRUE(first.interview_called);
    TEST_ASSERT_TRUE(first.bind_called);
    TEST_ASSERT_TRUE(first.configure_called);
    TEST_ASSERT_TRUE(first.raw_report_called);
    TEST_ASSERT_EQUAL_HEX16(short_addr, first.short_addr);
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_RESULT_SUCCESS, first.result);
    TEST_ASSERT_EQUAL_HEX16(0x0402U, first.cluster_id);
    TEST_ASSERT_EQUAL_HEX16(0x0000U, first.attribute_id);
    TEST_ASSERT_EQUAL_UINT8(sizeof(first_payload), first.payload_len);
    TEST_ASSERT_EQUAL_UINT8(first_payload[0], first.payload_first_byte);

    // Reboot recovery: runtime restarts and still receives join/report from already-paired device.
    reporting_flow_capture_t reboot = {0};
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, hal_zigbee_init());
    TEST_ASSERT_EQUAL_INT(HAL_ZIGBEE_STATUS_OK, hal_zigbee_register_callbacks(&callbacks, &reboot));

    hal_zigbee_notify_device_joined(short_addr);
    const hal_zigbee_raw_attribute_report_t reboot_report = {
        .short_addr = short_addr,
        .endpoint = 1U,
        .cluster_id = 0x0402U,
        .attribute_id = 0x0000U,
        .zcl_data_type = 0x29U,
        .has_lqi = true,
        .lqi = 190U,
        .has_rssi = true,
        .rssi_dbm = -63,
        .payload = reboot_payload,
        .payload_len = sizeof(reboot_payload),
    };
    hal_zigbee_notify_attribute_report_raw(&reboot_report);

    TEST_ASSERT_TRUE(reboot.joined_called);
    TEST_ASSERT_TRUE(reboot.raw_report_called);
    TEST_ASSERT_EQUAL_HEX16(short_addr, reboot.short_addr);
    TEST_ASSERT_EQUAL_HEX16(0x0402U, reboot.cluster_id);
    TEST_ASSERT_EQUAL_HEX16(0x0000U, reboot.attribute_id);
    TEST_ASSERT_EQUAL_UINT8(sizeof(reboot_payload), reboot.payload_len);
    TEST_ASSERT_EQUAL_UINT8(reboot_payload[0], reboot.payload_first_byte);
}
