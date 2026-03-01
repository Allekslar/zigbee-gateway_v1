/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_mdns.h"

#ifdef ESP_PLATFORM
#include <stdbool.h>

#include "esp_err.h"
#include "mdns.h"
#endif

int hal_mdns_start(const char* host_name) {
#ifdef ESP_PLATFORM
    static bool s_mdns_initialized = false;
#endif

    if (host_name == 0 || host_name[0] == '\0') {
        return -1;
    }

#ifdef ESP_PLATFORM
    if (!s_mdns_initialized) {
        const esp_err_t init_err = mdns_init();
        if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
            return -1;
        }
        s_mdns_initialized = true;
    }

    if (mdns_hostname_set(host_name) != ESP_OK) {
        return -1;
    }

    if (mdns_instance_name_set(host_name) != ESP_OK) {
        return -1;
    }

    return 0;
#else
    (void)host_name;
    return 0;
#endif
}
