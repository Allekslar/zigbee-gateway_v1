/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core_events.hpp"

namespace core {

class CoreEventBus {
public:
    using Handler = void (*)(const CoreEvent& event, void* context) noexcept;
    using SubscriptionId = uint16_t;

    bool subscribe(Handler handler, void* context, SubscriptionId* id_out = nullptr) noexcept;
    bool unsubscribe(SubscriptionId id) noexcept;
    void publish(const CoreEvent& event) const noexcept;
    std::size_t subscriber_count() const noexcept;

private:
    static constexpr std::size_t kMaxSubscribers = 16;

    struct Subscriber {
        SubscriptionId id{0};
        Handler handler{nullptr};
        void* context{nullptr};
        bool active{false};
    };

    std::array<Subscriber, kMaxSubscribers> subscribers_{};
    SubscriptionId next_id_{1};
    std::size_t count_{0};
};

}  // namespace core
