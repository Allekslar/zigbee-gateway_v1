/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_memory.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"

void* hal_alloc_internal_sram(size_t size_bytes) {
    // MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT alone is NOT enough to
    // exclude LP-RAM on ESP32-C6: confirmed by reading ESP-IDF's own
    // real memory_layout.c (components/heap/port/esp32c6/memory_layout.c)
    // -- the real "RTCRAM" (LP-RAM) memory type's own MEDIUM-priority
    // capability set is ESP32C6_MEM_COMMON_CAPS, which itself includes
    // MALLOC_CAP_INTERNAL and MALLOC_CAP_8BIT. Once the real "RAM" type's
    // (the actual internal SRAM) HIGH-priority pool can't satisfy a
    // request, the allocator falls through to RTCRAM's own matching
    // medium-priority entry -- exactly the real, on-hardware-confirmed
    // bug this function exists to prevent (see service_runtime.cpp's own
    // header comment for the HIL evidence). MALLOC_CAP_DMA is the one
    // capability present in the real "RAM" type's high-priority set that
    // RTCRAM's capability sets never include at any priority tier
    // (confirmed against the same real source) -- adding it here pins
    // this allocation to real internal SRAM only; if that pool can't
    // satisfy the request, this call fails closed (returns NULL) instead
    // of silently landing in a region too small to safely host a task
    // stack.
    return heap_caps_malloc(size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
}

void hal_free_internal_sram(void* ptr) {
    heap_caps_free(ptr);
}
#else
#include <stdlib.h>

// Host builds have no MALLOC_CAP_INTERNAL/LP-RAM distinction at all --
// plain malloc()/free() is the honest host equivalent, matching this
// project's established convention of a real, non-mocked implementation
// on host wherever the underlying operation has a direct host analog
// (unlike e.g. hal_tls_validate_certificate(), which has no safe host
// substitute for real crypto).
void* hal_alloc_internal_sram(size_t size_bytes) {
    return malloc(size_bytes);
}

void hal_free_internal_sram(void* ptr) {
    free(ptr);
}
#endif
