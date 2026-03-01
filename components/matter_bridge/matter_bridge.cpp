/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "matter_bridge.hpp"

namespace matter_bridge {

bool MatterBridge::start() noexcept {
    started_ = true;
    return started_;
}

void MatterBridge::stop() noexcept {
    started_ = false;
}

bool MatterBridge::started() const noexcept {
    return started_;
}

}  // namespace matter_bridge
