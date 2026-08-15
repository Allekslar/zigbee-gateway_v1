/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "hal_button_test.h"

int main() {
    // Fail-closed default: "not pressed" until a test explicitly injects
    // otherwise.
    assert(!hal_button_is_pressed());

    hal_button_set_mock_pressed(1);
    assert(hal_button_is_pressed());

    hal_button_set_mock_pressed(0);
    assert(!hal_button_is_pressed());

    return 0;
}
