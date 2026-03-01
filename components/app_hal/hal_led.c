/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_led.h"

#ifdef ESP_PLATFORM
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"
#endif

hal_led_status_t hal_led_set(bool on) {
#ifdef ESP_PLATFORM
    static bool s_led_initialized = false;
    static const gpio_num_t s_led_gpio = GPIO_NUM_8;

    if (!s_led_initialized) {
        gpio_config_t config = {
            .pin_bit_mask = (1ULL << s_led_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&config) != ESP_OK) {
            return HAL_LED_STATUS_ERR;
        }
        s_led_initialized = true;
    }

    return gpio_set_level(s_led_gpio, on ? 1 : 0) == ESP_OK ? HAL_LED_STATUS_OK : HAL_LED_STATUS_ERR;
#else
    (void)on;
    return HAL_LED_STATUS_OK;
#endif
}
