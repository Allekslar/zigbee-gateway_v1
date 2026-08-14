/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "session_store.hpp"

namespace {

using service::SessionCreateResult;
using service::SessionRecord;
using service::SessionStoreState;

constexpr uint64_t kIdleTimeoutMs = (uint64_t)service::kSessionIdleTimeoutSeconds * 1000ULL;
constexpr uint64_t kAbsoluteTimeoutMs = (uint64_t)service::kSessionAbsoluteTimeoutSeconds * 1000ULL;

void test_create_succeeds_with_correctly_shaped_fields() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 1000ULL, &record) == SessionCreateResult::kCreated);
    assert(record.in_use);
    assert(std::strlen(record.session_id_hex) == service::kSessionIdHexChars);
    assert(std::strlen(record.csrf_token_hex) == service::kCsrfTokenHexChars);
    assert(record.created_at_ms == 1000ULL);
    assert(record.last_active_at_ms == 1000ULL);
}

void test_create_rejects_null_args() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(nullptr, 0ULL, &record) == SessionCreateResult::kRngFailed);
    assert(service::session_store_create(&state, 0ULL, nullptr) == SessionCreateResult::kRngFailed);
}

void test_two_created_sessions_have_different_id_and_csrf_token() {
    SessionStoreState state{};
    SessionRecord first{};
    SessionRecord second{};
    assert(service::session_store_create(&state, 0ULL, &first) == SessionCreateResult::kCreated);
    assert(service::session_store_create(&state, 0ULL, &second) == SessionCreateResult::kCreated);
    assert(std::strcmp(first.session_id_hex, second.session_id_hex) != 0);
    assert(std::strcmp(first.csrf_token_hex, second.csrf_token_hex) != 0);
}

void test_is_valid_true_immediately_after_create() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 5000ULL, &record) == SessionCreateResult::kCreated);
    assert(service::session_store_is_valid(state, record.session_id_hex, 5000ULL));
}

void test_is_valid_false_for_unknown_or_null_or_empty_id() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    assert(!service::session_store_is_valid(state, "0000000000000000000000000000dead", 0ULL));
    assert(!service::session_store_is_valid(state, nullptr, 0ULL));
    assert(!service::session_store_is_valid(state, "", 0ULL));
}

void test_is_valid_false_once_idle_timeout_elapses_without_touch() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    assert(service::session_store_is_valid(state, record.session_id_hex, kIdleTimeoutMs - 1ULL));
    assert(!service::session_store_is_valid(state, record.session_id_hex, kIdleTimeoutMs));
}

void test_touch_extends_the_idle_timeout() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);

    const uint64_t just_before_idle_expiry = kIdleTimeoutMs - 1ULL;
    assert(service::session_store_touch(&state, record.session_id_hex, just_before_idle_expiry));

    // Idle timeout renewed from just_before_idle_expiry -- still valid at
    // a point that would have been expired relative to the ORIGINAL
    // created_at_ms=0 idle window, but is not relative to the touch.
    const uint64_t still_within_renewed_window = just_before_idle_expiry + kIdleTimeoutMs - 1ULL;
    assert(service::session_store_is_valid(state, record.session_id_hex, still_within_renewed_window));
}

void test_touch_does_not_extend_the_absolute_timeout() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);

    // Touch repeatedly, staying just inside the idle window each time,
    // until the absolute timeout is reached -- the absolute timeout must
    // still fire even though the session was never idle.
    uint64_t t = 0ULL;
    while (t + (kIdleTimeoutMs - 1ULL) < kAbsoluteTimeoutMs) {
        t += (kIdleTimeoutMs - 1ULL);
        assert(service::session_store_touch(&state, record.session_id_hex, t));
    }
    assert(!service::session_store_is_valid(state, record.session_id_hex, kAbsoluteTimeoutMs));
}

void test_touch_fails_on_unknown_or_already_expired_session() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    assert(!service::session_store_touch(&state, "unknown-session-id", 0ULL));
    assert(!service::session_store_touch(&state, record.session_id_hex, kIdleTimeoutMs));  // expired
}

void test_revoke_invalidates_and_is_idempotent() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    assert(service::session_store_is_valid(state, record.session_id_hex, 0ULL));

    service::session_store_revoke(&state, record.session_id_hex);
    assert(!service::session_store_is_valid(state, record.session_id_hex, 0ULL));

    // Idempotent: revoking again (already gone) must not crash/misbehave.
    service::session_store_revoke(&state, record.session_id_hex);
    service::session_store_revoke(&state, "never-existed-at-all");
}

void test_get_csrf_token_round_trips_and_rejects_unknown_or_expired() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);

    char csrf[service::kCsrfTokenHexChars + 1U]{};
    assert(service::session_store_get_csrf_token(state, record.session_id_hex, 0ULL, csrf, sizeof(csrf)));
    assert(std::strcmp(csrf, record.csrf_token_hex) == 0);

    assert(!service::session_store_get_csrf_token(state, "unknown", 0ULL, csrf, sizeof(csrf)));
    assert(!service::session_store_get_csrf_token(state, record.session_id_hex, kIdleTimeoutMs, csrf, sizeof(csrf)));

    char too_small[4]{};
    assert(!service::session_store_get_csrf_token(state, record.session_id_hex, 0ULL, too_small, sizeof(too_small)));
}

void test_store_is_full_after_max_concurrent_sessions_and_rejects_a_fifth() {
    SessionStoreState state{};
    for (uint32_t i = 0; i < service::kMaxConcurrentSessions; ++i) {
        SessionRecord record{};
        assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    }
    SessionRecord fifth{};
    assert(service::session_store_create(&state, 0ULL, &fifth) == SessionCreateResult::kFull);
}

void test_revoking_one_session_frees_capacity_for_a_new_one() {
    SessionStoreState state{};
    SessionRecord first{};
    assert(service::session_store_create(&state, 0ULL, &first) == SessionCreateResult::kCreated);
    for (uint32_t i = 1; i < service::kMaxConcurrentSessions; ++i) {
        SessionRecord record{};
        assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    }
    SessionRecord blocked{};
    assert(service::session_store_create(&state, 0ULL, &blocked) == SessionCreateResult::kFull);

    service::session_store_revoke(&state, first.session_id_hex);

    SessionRecord fits_now{};
    assert(service::session_store_create(&state, 0ULL, &fits_now) == SessionCreateResult::kCreated);
}

void test_an_expired_session_slot_is_reclaimed_by_a_new_create() {
    SessionStoreState state{};
    for (uint32_t i = 0; i < service::kMaxConcurrentSessions; ++i) {
        SessionRecord record{};
        assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    }
    SessionRecord blocked{};
    assert(service::session_store_create(&state, 0ULL, &blocked) == SessionCreateResult::kFull);

    // All 4 sessions idle-expire at kIdleTimeoutMs (never touched) --
    // session_store_create()'s own implicit sweep must reclaim a slot.
    SessionRecord fits_after_expiry{};
    assert(service::session_store_create(&state, kIdleTimeoutMs, &fits_after_expiry) == SessionCreateResult::kCreated);
}

void test_sweep_expired_clears_only_expired_slots() {
    SessionStoreState state{};
    SessionRecord stays{};
    SessionRecord expires{};
    assert(service::session_store_create(&state, 0ULL, &expires) == SessionCreateResult::kCreated);
    assert(service::session_store_create(&state, kIdleTimeoutMs - 1ULL, &stays) == SessionCreateResult::kCreated);

    service::session_store_sweep_expired(&state, kIdleTimeoutMs);

    assert(!service::session_store_is_valid(state, expires.session_id_hex, kIdleTimeoutMs));
    assert(service::session_store_is_valid(state, stays.session_id_hex, kIdleTimeoutMs));
}

}  // namespace

int main() {
    test_create_succeeds_with_correctly_shaped_fields();
    test_create_rejects_null_args();
    test_two_created_sessions_have_different_id_and_csrf_token();
    test_is_valid_true_immediately_after_create();
    test_is_valid_false_for_unknown_or_null_or_empty_id();
    test_is_valid_false_once_idle_timeout_elapses_without_touch();
    test_touch_extends_the_idle_timeout();
    test_touch_does_not_extend_the_absolute_timeout();
    test_touch_fails_on_unknown_or_already_expired_session();
    test_revoke_invalidates_and_is_idempotent();
    test_get_csrf_token_round_trips_and_rejects_unknown_or_expired();
    test_store_is_full_after_max_concurrent_sessions_and_rejects_a_fifth();
    test_revoking_one_session_frees_capacity_for_a_new_one();
    test_an_expired_session_slot_is_reclaimed_by_a_new_create();
    test_sweep_expired_clears_only_expired_slots();
    return 0;
}
