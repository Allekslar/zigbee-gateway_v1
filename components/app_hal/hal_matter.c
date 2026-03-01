/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_matter.h"

#ifdef ESP_PLATFORM
// Weak hooks for a platform-specific Matter stack adapter.
int __attribute__((weak)) hal_matter_stack_init(void) {
    return -1;
}

int __attribute__((weak)) hal_matter_stack_publish_state(uint16_t endpoint_id, bool on) {
    (void)endpoint_id;
    (void)on;
    return -1;
}
#endif

int hal_matter_init(void) {
#ifdef ESP_PLATFORM
    return hal_matter_stack_init();
#else
    return 0;
#endif
}

int hal_matter_publish_state(uint16_t endpoint_id, bool on) {
#ifdef ESP_PLATFORM
    return hal_matter_stack_publish_state(endpoint_id, on);
#else
    (void)endpoint_id;
    (void)on;
    return 0;
#endif
}
