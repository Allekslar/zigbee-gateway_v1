/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "tuya_init_coordinator.hpp"

using namespace service;

static TuyaInitPlan make_single_step_plan() {
    TuyaInitPlan plan{};
    plan.step_count = 1;
    plan.steps[0].dp_id = 10;
    plan.steps[0].dp_type = TuyaDpType::kValue;
    plan.steps[0].value[0] = 0x01;
    plan.steps[0].value_len = 1;
    plan.steps[0].endpoint = 1;
    return plan;
}

static TuyaInitPlan make_two_step_plan() {
    TuyaInitPlan plan{};
    plan.step_count = 2;
    plan.steps[0].dp_id = 10;
    plan.steps[0].dp_type = TuyaDpType::kValue;
    plan.steps[0].value[0] = 0x01;
    plan.steps[0].value_len = 1;
    plan.steps[0].endpoint = 1;
    plan.steps[1].dp_id = 20;
    plan.steps[1].dp_type = TuyaDpType::kBool;
    plan.steps[1].value[0] = 0x01;
    plan.steps[1].value_len = 1;
    plan.steps[1].endpoint = 1;
    return plan;
}

int main() {
    /* Empty plan transitions to kReady immediately */
    {
        TuyaInitCoordinator coordinator{};
        TuyaInitPlan plan{};
        coordinator.notify_device_resolved(0x1234, plan);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kReady);

        TuyaInitAction action = coordinator.tick(1000);
        assert(!action.pending);
    }

    /* Unknown device returns kNotStarted */
    {
        TuyaInitCoordinator coordinator{};
        assert(coordinator.status(0x9999) == TuyaInitStatus::kNotStarted);
    }

    /* Single step: pending -> tick emits action -> ACK success -> kReady */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());
        assert(coordinator.status(0x1234) == TuyaInitStatus::kInitPending);

        TuyaInitAction action = coordinator.tick(1000);
        assert(action.pending);
        assert(action.short_addr == 0x1234);
        assert(action.dp_id == 10);
        assert(action.dp_type == TuyaDpType::kValue);
        assert(action.value[0] == 0x01);
        assert(action.value_len == 1);
        assert(action.endpoint == 1);
        assert(action.correlation_id >= kTuyaInitCorrelationIdBase);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kWaitingAck);

        bool handled = coordinator.notify_ack(action.correlation_id, true);
        assert(handled);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kReady);
    }

    /* Multi step: step through both steps */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_two_step_plan());

        TuyaInitAction a1 = coordinator.tick(1000);
        assert(a1.pending);
        assert(a1.dp_id == 10);
        coordinator.notify_ack(a1.correlation_id, true);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kInitPending);

        TuyaInitAction a2 = coordinator.tick(2000);
        assert(a2.pending);
        assert(a2.dp_id == 20);
        assert(a2.dp_type == TuyaDpType::kBool);
        coordinator.notify_ack(a2.correlation_id, true);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kReady);
    }

    /* Timeout triggers retry */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());

        TuyaInitAction a1 = coordinator.tick(1000);
        assert(a1.pending);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kWaitingAck);

        TuyaInitAction none = coordinator.tick(1000 + kTuyaInitStepTimeoutMs - 1);
        assert(!none.pending);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kWaitingAck);

        TuyaInitAction retry = coordinator.tick(1000 + kTuyaInitStepTimeoutMs);
        assert(retry.pending);
        assert(retry.dp_id == 10);
        assert(retry.correlation_id != a1.correlation_id);
    }

    /* Retries exhausted -> kDegraded */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());

        uint32_t t = 1000;
        for (uint8_t i = 0; i <= kTuyaInitMaxRetries; ++i) {
            TuyaInitAction a = coordinator.tick(t);
            assert(a.pending);
            t += kTuyaInitStepTimeoutMs;
        }

        TuyaInitAction timeout_tick = coordinator.tick(t);
        (void)timeout_tick;
        assert(coordinator.status(0x1234) == TuyaInitStatus::kDegraded);
    }

    /* ACK failure triggers retry */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());

        TuyaInitAction a1 = coordinator.tick(1000);
        assert(a1.pending);
        bool handled = coordinator.notify_ack(a1.correlation_id, false);
        assert(handled);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kInitPending);

        TuyaInitAction a2 = coordinator.tick(2000);
        assert(a2.pending);
        assert(a2.dp_id == 10);
    }

    /* ACK failure retries exhausted -> kDegraded */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());

        for (uint8_t i = 0; i <= kTuyaInitMaxRetries; ++i) {
            TuyaInitAction a = coordinator.tick(1000 + i * 100);
            assert(a.pending);
            coordinator.notify_ack(a.correlation_id, false);
        }
        assert(coordinator.status(0x1234) == TuyaInitStatus::kDegraded);
    }

    /* Device removed clears entry */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());
        assert(coordinator.status(0x1234) == TuyaInitStatus::kInitPending);

        coordinator.notify_device_removed(0x1234);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kNotStarted);
    }

    /* Unmatched correlation_id returns false */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());
        coordinator.tick(1000);

        bool handled = coordinator.notify_ack(0xDEADBEEF, true);
        assert(!handled);
    }

    /* No action when no pending entries */
    {
        TuyaInitCoordinator coordinator{};
        TuyaInitAction action = coordinator.tick(1000);
        assert(!action.pending);
    }

    /* Multiple devices tracked simultaneously */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1111, make_single_step_plan());
        coordinator.notify_device_resolved(0x2222, make_two_step_plan());

        TuyaInitAction a1 = coordinator.tick(1000);
        assert(a1.pending);
        assert(a1.short_addr == 0x1111);
        coordinator.notify_ack(a1.correlation_id, true);
        assert(coordinator.status(0x1111) == TuyaInitStatus::kReady);

        TuyaInitAction a2 = coordinator.tick(2000);
        assert(a2.pending);
        assert(a2.short_addr == 0x2222);
        coordinator.notify_ack(a2.correlation_id, true);
        assert(coordinator.status(0x2222) == TuyaInitStatus::kInitPending);

        TuyaInitAction a3 = coordinator.tick(3000);
        assert(a3.pending);
        assert(a3.short_addr == 0x2222);
        coordinator.notify_ack(a3.correlation_id, true);
        assert(coordinator.status(0x2222) == TuyaInitStatus::kReady);
    }

    /* clear() resets all entries */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_single_step_plan());
        coordinator.clear();
        assert(coordinator.status(0x1234) == TuyaInitStatus::kNotStarted);
    }

    /* Retries reset after successful step advance */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1234, make_two_step_plan());

        TuyaInitAction a1 = coordinator.tick(1000);
        assert(a1.pending);
        coordinator.notify_ack(a1.correlation_id, false);
        coordinator.notify_ack(coordinator.tick(2000).correlation_id, false);

        TuyaInitAction a3 = coordinator.tick(3000);
        assert(a3.pending);
        coordinator.notify_ack(a3.correlation_id, true);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kInitPending);

        for (uint8_t i = 0; i <= kTuyaInitMaxRetries; ++i) {
            TuyaInitAction a = coordinator.tick(4000 + i * 100);
            assert(a.pending);
            coordinator.notify_ack(a.correlation_id, false);
        }
        assert(coordinator.status(0x1234) == TuyaInitStatus::kDegraded);
    }

    /* Re-resolve same device resets state */
    {
        TuyaInitCoordinator coordinator{};
        TuyaInitPlan plan = make_single_step_plan();
        coordinator.notify_device_resolved(0x1234, plan);
        coordinator.tick(1000);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kWaitingAck);

        TuyaInitPlan empty{};
        coordinator.notify_device_resolved(0x1234, empty);
        assert(coordinator.status(0x1234) == TuyaInitStatus::kReady);
    }

    /* Tick emits only one action per call */
    {
        TuyaInitCoordinator coordinator{};
        coordinator.notify_device_resolved(0x1111, make_single_step_plan());
        coordinator.notify_device_resolved(0x2222, make_single_step_plan());

        TuyaInitAction a1 = coordinator.tick(1000);
        assert(a1.pending);
        assert(coordinator.status(a1.short_addr) == TuyaInitStatus::kWaitingAck);

        uint16_t other = (a1.short_addr == 0x1111) ? 0x2222 : 0x1111;
        assert(coordinator.status(other) == TuyaInitStatus::kInitPending);
    }

    /* init_plan() default returns empty plan */
    {
        struct TestPlugin final : TuyaPlugin {
            bool matches(const TuyaFingerprint&) const noexcept override { return true; }
            TuyaPluginResult translate(const TuyaFingerprint&, const TuyaDpParseResult&) const noexcept override {
                return TuyaPluginResult{};
            }
        };
        TestPlugin plugin{};
        TuyaFingerprint fp{};
        fp.manufacturer = "_TZtest";
        fp.model = "TS9999";
        TuyaInitPlan plan = plugin.init_plan(fp);
        assert(plan.empty());
    }

    return 0;
}
