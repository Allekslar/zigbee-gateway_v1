/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hal_matter_init(void);
int hal_matter_publish_state(uint16_t endpoint_id, bool on);

#ifdef __cplusplus
}
#endif
