/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Allocates `size_bytes` from internal SRAM specifically -- never PSRAM,
// never LP-RAM/RTC memory. Real, hardware-found need: plain FreeRTOS
// xTaskCreate()'s own default allocator can silently fall back to
// LP-RAM (a ~16KB region meant for ULP/RTC-domain code, far too small
// to safely host a general task stack) when the preferred internal-SRAM
// pool is fragmented enough at allocation time -- xTaskCreate() itself
// has no way to refuse that fallback and reports success either way.
// service_runtime.cpp's own real-hardware "Stack protection fault"
// finding (a task's stack landing in LP-RAM) is the concrete case this
// exists for -- see that file's own comment on the exact HIL evidence.
// Lives in app_hal, not components/service, because INV-H002 forbids
// the service layer from calling malloc/calloc/realloc/free directly;
// this is the same relocation hal_tls_certificate_validator.h's own
// header comment already documents for the identical class of gap.
// Returns nullptr on failure, exactly like malloc().
void* hal_alloc_internal_sram(size_t size_bytes);

// Frees memory obtained from hal_alloc_internal_sram(). A no-op on a
// nullptr argument, matching free()'s own contract.
void hal_free_internal_sram(void* ptr);

#ifdef __cplusplus
}
#endif
