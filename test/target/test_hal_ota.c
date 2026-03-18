/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_ota.h"
#include "unity.h"

void test_hal_ota_foundation_contract(void) {
    char version[32] = {0};

    TEST_ASSERT_EQUAL_INT(0, hal_ota_mark_running_partition_valid());
    TEST_ASSERT_FALSE(hal_ota_running_partition_pending_verify());
    TEST_ASSERT_TRUE(hal_ota_get_running_version(version, sizeof(version)));
    TEST_ASSERT_NOT_EQUAL('\0', version[0]);
}
