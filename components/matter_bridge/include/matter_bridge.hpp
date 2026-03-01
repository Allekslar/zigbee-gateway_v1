/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

namespace matter_bridge {

class MatterBridge {
public:
    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;

private:
    bool started_{false};
};

}  // namespace matter_bridge
