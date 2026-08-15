/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Plan S6 "Authorization and physical presence" #21: "Grant is created
// only from trusted GPIO/button event...". This is that raw GPIO read
// only -- debounce/edge-detection (turning a raw level into a single
// "trusted button event" a caller feeds to
// physical_presence_grant_create()) is deliberately NOT this module's
// job, matching commissioning_window.hpp's own already-documented
// precedent ("a caller with a real button interrupt/task would call
// commissioning_window_start() with kTrustedButtonAction once it has
// one") -- that caller/task does not exist yet in this repository
// (neither does one for this grant), so this HAL is defined ahead of its
// full pipeline, the same "port/state-machine before wiring" discipline
// every other S6 primitive has followed.
//
// ESP_PLATFORM: reads `CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO`
// (default 9 -- the real BOOT button on the ESP32-C6-DevKitC-1 reference
// board, confirmed via Espressif's own official documentation: GPIO9 is
// a strapping pin only during power-up/reset, safely readable as an
// ordinary input afterward). Configured with the internal pull-up
// enabled and read active-low (pressed == GPIO reads 0), the standard,
// documented wiring for every ESP32 dev-kit BOOT button. Host/test
// builds: mockable, default "not pressed" (fail closed -- see
// hal_button_test.h).
int hal_button_is_pressed(void);

#ifdef __cplusplus
}
#endif
