/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace core {

enum class CoreError : uint8_t {
    kOk = 0,
    kInvalidArgument,
    kNotFound,
    kBusy,
    kTimeout,
    kNoCapacity,
    kInternal,
};

}  // namespace core
