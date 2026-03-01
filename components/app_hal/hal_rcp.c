/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_rcp.h"

#ifdef ESP_PLATFORM
// Weak hooks for a platform-specific RCP updater implementation.
int __attribute__((weak)) hal_rcp_stack_update_begin(void) {
    return -1;
}

int __attribute__((weak)) hal_rcp_stack_update_write(const uint8_t* data, uint32_t len) {
    (void)data;
    (void)len;
    return -1;
}

int __attribute__((weak)) hal_rcp_stack_update_end(void) {
    return -1;
}
#endif

int hal_rcp_update_begin(void) {
#ifdef ESP_PLATFORM
    return hal_rcp_stack_update_begin();
#else
    return 0;
#endif
}

int hal_rcp_update_write(const uint8_t* data, uint32_t len) {
#ifdef ESP_PLATFORM
    return hal_rcp_stack_update_write(data, len);
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

int hal_rcp_update_end(void) {
#ifdef ESP_PLATFORM
    return hal_rcp_stack_update_end();
#else
    return 0;
#endif
}
