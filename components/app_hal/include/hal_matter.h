/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_MATTER_ATTR_AVAILABILITY_ONLINE = 0,
    HAL_MATTER_ATTR_TEMPERATURE_CENTI_C = 1,
    HAL_MATTER_ATTR_OCCUPANCY = 2,
    HAL_MATTER_ATTR_CONTACT_OPEN = 3,
    HAL_MATTER_ATTR_STALE = 4,
} hal_matter_attr_type_t;

typedef struct {
    uint16_t endpoint_id;
    hal_matter_attr_type_t attr_type;
    bool bool_value;
    int32_t int_value;
} hal_matter_attribute_update_t;

int hal_matter_init(void);
int hal_matter_publish_state(uint16_t endpoint_id, bool on);
int hal_matter_publish_attribute_update(const hal_matter_attribute_update_t* update);

#ifdef __cplusplus
}
#endif
