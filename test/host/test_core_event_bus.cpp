/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include "core_event_bus.hpp"

static int g_call_count = 0;
static void test_handler(const core::CoreEvent& event, void* context) noexcept {
    g_call_count++;
    *(static_cast<int*>(context)) = static_cast<int>(event.type);
}

int main() {
    core::CoreEventBus bus;
    int context = 0;
    core::CoreEventBus::SubscriptionId id1, id2;

    
    assert(bus.subscribe(test_handler, &context, &id1));
    assert(bus.subscribe(test_handler, &context, &id2));
    assert(id1 != id2);
    assert(bus.subscriber_count() == 2);

    
    core::CoreEvent ev{};
    ev.type = core::CoreEventType::kNetworkUp;
    bus.publish(ev);
    assert(g_call_count == 2);
    assert(context == static_cast<int>(core::CoreEventType::kNetworkUp));

    
    assert(bus.unsubscribe(id1));
    assert(bus.subscriber_count() == 1);
    
    g_call_count = 0;
    bus.publish(ev);
    assert(g_call_count == 1);

    
    for (int i = 0; i < 15; ++i) {
        bus.subscribe(test_handler, &context);
    }
    assert(bus.subscriber_count() == 16);
    assert(!bus.subscribe(test_handler, &context)); 

    return 0;
}