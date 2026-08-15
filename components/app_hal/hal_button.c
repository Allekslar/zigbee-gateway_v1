/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_button.h"

#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "sdkconfig.h"
#endif

// Test-only override hook (declared for external callers in
// hal_button_test.h, gated by SERVICE_RUNTIME_TEST_HOOKS there). Defined
// unconditionally here, matching hal_identity.c/hal_security_state.c's
// pattern; on ESP_PLATFORM the setter is a no-op so no build profile can
// be used to spoof a physical-presence event.
void hal_button_set_mock_pressed(int pressed);

#ifdef ESP_PLATFORM
// test/target is a separate ESP-IDF project (its own CMakeLists.txt/main/)
// that never sees the root main/Kconfig.projbuild -- CONFIG_ZGW_
// PHYSICAL_PRESENCE_BUTTON_GPIO is therefore undeclared there, exactly
// the same class of gap security_bounds.cpp's own kRanges fallback table
// already established the fix for in this codebase. Falls back to GPIO9
// (the Kconfig option's own default: the real ESP32-C6-DevKitC-1 BOOT
// button), never a made-up or different value.
#ifndef CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO
#define CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO 9
#endif

static bool s_gpio_configured = false;

static void ensure_gpio_configured(void) {
    if (s_gpio_configured) {
        return;
    }
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    // Deliberately does not fail hal_button_is_pressed() if gpio_config()
    // itself errors (e.g. an invalid GPIO chosen via Kconfig on a
    // different board) -- gpio_get_level() on an unconfigured pin still
    // returns a defined (if not pull-up-guaranteed) level rather than
    // crashing, and this HAL's contract is "best-effort raw read", not
    // "must succeed". s_gpio_configured is still set so this is only
    // attempted once per boot, matching hal_rcp_uart_backend.c's own
    // established one-shot-init convention.
    (void)gpio_config(&config);
    s_gpio_configured = true;
}
#else
static int s_mock_pressed = 0;
#endif

int hal_button_is_pressed(void) {
#ifdef ESP_PLATFORM
    ensure_gpio_configured();
    // Active-low: pressed pulls the pin to 0.
    return gpio_get_level((gpio_num_t)CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO) == 0;
#else
    return s_mock_pressed;
#endif
}

void hal_button_set_mock_pressed(int pressed) {
#ifndef ESP_PLATFORM
    s_mock_pressed = pressed;
#else
    (void)pressed;
#endif
}
