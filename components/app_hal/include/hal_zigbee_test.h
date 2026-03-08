/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(SERVICE_RUNTIME_TEST_HOOKS)
#error "hal_zigbee_test.h is test-only and requires SERVICE_RUNTIME_TEST_HOOKS"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_zigbee_test_apply_permit_join_status(uint8_t duration_seconds);

#ifdef __cplusplus
}
#endif
