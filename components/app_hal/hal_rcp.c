/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_rcp.h"

// Weak hooks for a platform-specific RCP updater implementation.
int __attribute__((weak)) hal_rcp_stack_update_begin(void) {
#ifdef ESP_PLATFORM
    return -1;
#else
    return 0;
#endif
}

int __attribute__((weak)) hal_rcp_stack_update_write(const uint8_t* data, uint32_t len) {
#ifdef ESP_PLATFORM
    (void)data;
    (void)len;
    return -1;
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

int __attribute__((weak)) hal_rcp_stack_update_end(void) {
#ifdef ESP_PLATFORM
    return -1;
#else
    return 0;
#endif
}

int hal_rcp_update_begin(void) {
    return hal_rcp_stack_update_begin();
}

int hal_rcp_update_write(const uint8_t* data, uint32_t len) {
    return hal_rcp_stack_update_write(data, len);
}

int hal_rcp_update_end(void) {
    return hal_rcp_stack_update_end();
}
