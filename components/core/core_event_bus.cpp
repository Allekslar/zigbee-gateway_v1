/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_event_bus.hpp"

namespace core {

bool CoreEventBus::subscribe(Handler handler, void* context, SubscriptionId* id_out) noexcept {
    if (handler == nullptr || count_ >= kMaxSubscribers) {
        return false;
    }

    for (std::size_t i = 0; i < kMaxSubscribers; ++i) {
        Subscriber& subscriber = subscribers_[i];
        if (subscriber.active) {
            continue;
        }

        subscriber.id = next_id_++;
        if (next_id_ == 0) {
            next_id_ = 1;
        }

        subscriber.handler = handler;
        subscriber.context = context;
        subscriber.active = true;
        ++count_;

        if (id_out != nullptr) {
            *id_out = subscriber.id;
        }
        return true;
    }

    return false;
}

bool CoreEventBus::unsubscribe(SubscriptionId id) noexcept {
    if (id == 0) {
        return false;
    }

    for (std::size_t i = 0; i < kMaxSubscribers; ++i) {
        Subscriber& subscriber = subscribers_[i];
        if (!subscriber.active || subscriber.id != id) {
            continue;
        }

        subscriber = Subscriber{};
        if (count_ > 0) {
            --count_;
        }
        return true;
    }

    return false;
}

void CoreEventBus::publish(const CoreEvent& event) const noexcept {
    for (std::size_t i = 0; i < kMaxSubscribers; ++i) {
        const Subscriber& subscriber = subscribers_[i];
        if (!subscriber.active || subscriber.handler == nullptr) {
            continue;
        }

        subscriber.handler(event, subscriber.context);
    }
}

std::size_t CoreEventBus::subscriber_count() const noexcept {
    return count_;
}

}  // namespace core
