/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_LED_STATUS_OK = 0,
    HAL_LED_STATUS_ERR = -1,
} hal_led_status_t;

hal_led_status_t hal_led_set(bool on);

#ifdef __cplusplus
}
#endif
