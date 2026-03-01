/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

namespace service {

class ServiceRuntime;

bool init_hal_event_adapter(ServiceRuntime& runtime) noexcept;

}  // namespace service
