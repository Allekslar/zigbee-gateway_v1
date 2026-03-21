/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_rcp.h"

#ifdef ESP_PLATFORM

#include "sdkconfig.h"

#if defined(CONFIG_ZGW_RCP_TARGET_BACKEND_UART) && CONFIG_ZGW_RCP_TARGET_BACKEND_UART

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * This backend is optional groundwork for future hardware that contains a
 * separate external RCP reachable over UART plus optional BOOT/RESET control
 * lines. It is not part of the active production path for the current
 * ESP32-C6-DevKitC board, which uses the native 802.15.4 radio.
 */

constexpr const char* kTag = "hal_rcp_uart";
constexpr const char* kBackendName = "uart-maintenance";
constexpr int kUartRxBufferSize = 4096;
constexpr TickType_t kWriteTimeoutTicks = pdMS_TO_TICKS(1000);

bool s_uart_installed = false;
bool s_session_prepared = false;

constexpr int kConfiguredPort = CONFIG_ZGW_RCP_UART_PORT;
constexpr int kConfiguredTxGpio = CONFIG_ZGW_RCP_UART_TX_GPIO;
constexpr int kConfiguredRxGpio = CONFIG_ZGW_RCP_UART_RX_GPIO;
constexpr int kConfiguredBaudrate = CONFIG_ZGW_RCP_UART_BAUDRATE;
constexpr int kConfiguredBootGpio = CONFIG_ZGW_RCP_UART_BOOT_GPIO;
constexpr int kConfiguredResetGpio = CONFIG_ZGW_RCP_UART_RESET_GPIO;
constexpr TickType_t kEnterUpdateDelayTicks = pdMS_TO_TICKS(CONFIG_ZGW_RCP_UART_ENTER_UPDATE_DELAY_MS);
constexpr TickType_t kResetPulseTicks = pdMS_TO_TICKS(CONFIG_ZGW_RCP_UART_RESET_PULSE_MS);

bool gpio_configured(const int gpio_num) {
    return gpio_num >= 0;
}

int inactive_level(const bool active_high) {
    return active_high ? 0 : 1;
}

int active_level(const bool active_high) {
    return active_high ? 1 : 0;
}

esp_err_t configure_optional_output_gpio(const int gpio_num, const bool active_high) {
    if (!gpio_configured(gpio_num)) {
        return ESP_OK;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level((gpio_num_t)gpio_num, inactive_level(active_high));
}

bool backend_config_valid(void) {
    return kConfiguredTxGpio >= 0 && kConfiguredRxGpio >= 0 && kConfiguredTxGpio != kConfiguredRxGpio &&
           kConfiguredBaudrate > 0 && kConfiguredPort >= 0;
}

esp_err_t ensure_uart_driver(void) {
    if (s_uart_installed) {
        return ESP_OK;
    }

    const uart_config_t config = {
        .baud_rate = kConfiguredBaudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install((uart_port_t)kConfiguredPort, kUartRxBufferSize, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_param_config((uart_port_t)kConfiguredPort, &config);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_set_pin(
        (uart_port_t)kConfiguredPort, kConfiguredTxGpio, kConfiguredRxGpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    s_uart_installed = true;
    return ESP_OK;
}

esp_err_t set_boot_mode_level(const bool active) {
    if (!gpio_configured(kConfiguredBootGpio)) {
        return ESP_OK;
    }
    return gpio_set_level(
        (gpio_num_t)kConfiguredBootGpio, active ? active_level(CONFIG_ZGW_RCP_UART_BOOT_ACTIVE_HIGH)
                                                : inactive_level(CONFIG_ZGW_RCP_UART_BOOT_ACTIVE_HIGH));
}

esp_err_t set_reset_level(const bool active) {
    if (!gpio_configured(kConfiguredResetGpio)) {
        return ESP_OK;
    }
    return gpio_set_level(
        (gpio_num_t)kConfiguredResetGpio, active ? active_level(CONFIG_ZGW_RCP_UART_RESET_ACTIVE_HIGH)
                                                 : inactive_level(CONFIG_ZGW_RCP_UART_RESET_ACTIVE_HIGH));
}

esp_err_t enter_update_mode(void) {
    esp_err_t err = configure_optional_output_gpio(kConfiguredBootGpio, CONFIG_ZGW_RCP_UART_BOOT_ACTIVE_HIGH);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_optional_output_gpio(kConfiguredResetGpio, CONFIG_ZGW_RCP_UART_RESET_ACTIVE_HIGH);
    if (err != ESP_OK) {
        return err;
    }

    err = set_boot_mode_level(true);
    if (err != ESP_OK) {
        return err;
    }

    if (gpio_configured(kConfiguredResetGpio)) {
        err = set_reset_level(true);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(kResetPulseTicks);
        err = set_reset_level(false);
        if (err != ESP_OK) {
            return err;
        }
    }

    vTaskDelay(kEnterUpdateDelayTicks);
    err = set_boot_mode_level(false);
    if (err != ESP_OK) {
        return err;
    }

    uart_flush_input((uart_port_t)kConfiguredPort);
    return ESP_OK;
}

esp_err_t reboot_target_after_update(void) {
    if (!gpio_configured(kConfiguredResetGpio)) {
        return ESP_OK;
    }

    esp_err_t err = set_boot_mode_level(false);
    if (err != ESP_OK) {
        return err;
    }
    err = set_reset_level(true);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(kResetPulseTicks);
    return set_reset_level(false);
}

bool hal_rcp_stack_backend_available(void) {
    return backend_config_valid();
}

bool hal_rcp_stack_get_backend_name(char* out, size_t out_len) {
    if (out == NULL || out_len < (sizeof("uart-maintenance"))) {
        return false;
    }
    memcpy(out, kBackendName, sizeof("uart-maintenance"));
    return true;
}

int hal_rcp_stack_prepare_for_update(void) {
    if (!backend_config_valid()) {
        ESP_LOGW(kTag, "RCP UART backend unavailable: configure TX/RX GPIO and UART port");
        return -1;
    }

    esp_err_t err = ensure_uart_driver();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize RCP UART driver err=0x%x", (unsigned)err);
        return -1;
    }

    err = enter_update_mode();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to enter RCP update mode err=0x%x", (unsigned)err);
        return -1;
    }

    s_session_prepared = true;
    ESP_LOGI(
        kTag,
        "RCP UART backend prepared port=%d tx=%d rx=%d baud=%d boot_gpio=%d reset_gpio=%d",
        kConfiguredPort,
        kConfiguredTxGpio,
        kConfiguredRxGpio,
        kConfiguredBaudrate,
        kConfiguredBootGpio,
        kConfiguredResetGpio);
    return 0;
}

int hal_rcp_stack_update_begin(void) {
    if (!s_session_prepared) {
        ESP_LOGW(kTag, "RCP update begin requested before prepare");
        return -1;
    }
    uart_flush_input((uart_port_t)kConfiguredPort);
    return 0;
}

int hal_rcp_stack_update_write(const uint8_t* data, uint32_t len) {
    if (!s_session_prepared || data == NULL || len == 0U) {
        return -1;
    }

    const int written = uart_write_bytes((uart_port_t)kConfiguredPort, data, len);
    if (written < 0 || (uint32_t)written != len) {
        ESP_LOGE(kTag, "RCP UART write failed expected=%" PRIu32 " actual=%d", len, written);
        return -1;
    }

    if (uart_wait_tx_done((uart_port_t)kConfiguredPort, kWriteTimeoutTicks) != ESP_OK) {
        ESP_LOGE(kTag, "RCP UART write timeout len=%" PRIu32, len);
        return -1;
    }

    return 0;
}

int hal_rcp_stack_update_end(void) {
    if (!s_session_prepared) {
        return -1;
    }
    return uart_wait_tx_done((uart_port_t)kConfiguredPort, kWriteTimeoutTicks) == ESP_OK ? 0 : -1;
}

int hal_rcp_stack_recover_after_update(bool update_applied) {
    if (!backend_config_valid()) {
        return -1;
    }

    if (update_applied) {
        if (reboot_target_after_update() != ESP_OK) {
            s_session_prepared = false;
            return -1;
        }
    } else {
        (void)set_boot_mode_level(false);
        (void)set_reset_level(false);
    }

    s_session_prepared = false;
    return 0;
}

#endif  // CONFIG_ZGW_RCP_TARGET_BACKEND_UART

#endif  // ESP_PLATFORM
