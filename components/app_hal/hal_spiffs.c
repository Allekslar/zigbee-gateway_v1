/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_spiffs.h"

#ifdef ESP_PLATFORM
#include <stdbool.h>

#include "esp_err.h"
#include "esp_spiffs.h"
#endif

int hal_spiffs_mount(void) {
#ifdef ESP_PLATFORM
    static bool s_spiffs_mounted = false;

    if (s_spiffs_mounted) {
        return 0;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = 0,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_spiffs_mounted = true;
        return 0;
    }

    return -1;
#else
    return 0;
#endif
}
