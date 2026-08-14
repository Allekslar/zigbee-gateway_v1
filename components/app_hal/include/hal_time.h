/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Monotonic milliseconds since an arbitrary, boot-stable epoch -- never
// wall-clock time, never affected by clock adjustment. Used by plan S6
// #3 (commissioning-window expiry) and #5 (PBKDF2 iteration-count
// calibration timing).
//
// ESP_PLATFORM: wraps ESP-IDF's real `esp_timer_get_time()`
// (`esp_timer/include/esp_timer.h`, confirmed signature
// `int64_t esp_timer_get_time(void)` -- microseconds since boot -- against
// the real header inside `espressif/idf:release-v5.5` before writing this
// declaration; `esp_timer` is already an `app_hal` `REQUIRES` dependency).
// Host builds: wraps POSIX `clock_gettime(CLOCK_MONOTONIC, ...)`.
uint64_t hal_time_now_ms(void);

#ifdef __cplusplus
}
#endif
