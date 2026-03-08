/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <stdint.h>
#include <string.h>

#include "hal_nvs.h"
#include "unity.h"

static uint32_t s_last_nvs_value = 0;
static int s_nvs_callback_calls = 0;
static char s_last_nvs_key[32];

static void on_nvs_u32_written(void* context, const char* key, uint32_t value) {
    (void)context;
    ++s_nvs_callback_calls;
    s_last_nvs_value = value;

    strncpy(s_last_nvs_key, key, sizeof(s_last_nvs_key) - 1);
    s_last_nvs_key[sizeof(s_last_nvs_key) - 1] = '\0';
}

void test_hal_nvs_set_get_roundtrip(void) {
    s_last_nvs_value = 0;
    s_nvs_callback_calls = 0;
    s_last_nvs_key[0] = '\0';

    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_init());

    hal_nvs_callbacks_t callbacks = {
        .on_u32_written = on_nvs_u32_written,
    };
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_register_callbacks(&callbacks, 0));

    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_set_u32("core_rev", 77));

    uint32_t value = 0;
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_get_u32("core_rev", &value));
    TEST_ASSERT_EQUAL_UINT32(77, value);
    TEST_ASSERT_EQUAL_INT(1, s_nvs_callback_calls);
    TEST_ASSERT_EQUAL_UINT32(77, s_last_nvs_value);
    TEST_ASSERT_EQUAL_STRING("core_rev", s_last_nvs_key);
}

void test_hal_nvs_missing_key_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_init());

    uint32_t value = 0;
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_NOT_FOUND, hal_nvs_get_u32("missing_key", &value));
}

void test_hal_nvs_blob_roundtrip(void) {
    static const uint8_t expected[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint8_t actual[sizeof(expected)] = {0};
    uint32_t actual_len = 0U;

    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_init());
    TEST_ASSERT_EQUAL_INT(HAL_NVS_STATUS_OK, hal_nvs_set_blob("blob_key", expected, (uint32_t)sizeof(expected)));
    TEST_ASSERT_EQUAL_INT(
        HAL_NVS_STATUS_OK,
        hal_nvs_get_blob("blob_key", actual, (uint32_t)sizeof(actual), &actual_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), actual_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}
