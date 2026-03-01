/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "core_effects.hpp"

namespace service {

class EffectExecutor {
public:
    bool execute(const core::CoreEffect& effect) noexcept;
};

}  // namespace service
