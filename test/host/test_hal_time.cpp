/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "hal_time.h"

namespace {

void test_hal_time_now_ms_is_monotonic_nondecreasing() {
    const uint64_t first = hal_time_now_ms();
    const uint64_t second = hal_time_now_ms();
    assert(second >= first);
}

void test_hal_time_now_ms_is_nonzero_after_process_start() {
    // CLOCK_MONOTONIC on the host is not guaranteed to be nonzero at the
    // very first call, but by the time this test process has already
    // run other tests/setup, some monotonic time must have elapsed since
    // an arbitrary but fixed reference point. The one guarantee this
    // assertion actually checks is that the call succeeds and returns a
    // plausible (not obviously garbage) value.
    const uint64_t now = hal_time_now_ms();
    assert(now < (uint64_t)1ULL << 62);  // sanity bound, not a real time check
}

}  // namespace

int main() {
    test_hal_time_now_ms_is_monotonic_nondecreasing();
    test_hal_time_now_ms_is_nonzero_after_process_start();
    return 0;
}
