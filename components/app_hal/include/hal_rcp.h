/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hal_rcp_update_begin(void);
int hal_rcp_update_write(const uint8_t* data, uint32_t len);
int hal_rcp_update_end(void);

#ifdef __cplusplus
}
#endif
