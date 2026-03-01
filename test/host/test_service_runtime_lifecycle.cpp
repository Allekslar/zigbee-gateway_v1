/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "core_events.hpp"
#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    assert(runtime.start());
    assert(runtime.start());

    core::CoreEvent joined{};
    joined.type = core::CoreEventType::kDeviceJoined;
    joined.device_short_addr = 0x4411;
    assert(runtime.post_event(joined));
    assert(runtime.process_pending() == 1);
    assert(runtime.state().device_count == 1);

    return 0;
}
