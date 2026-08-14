/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "session_store.hpp"

#include <cstring>

#include "provisioning_secrets.hpp"

namespace service {

namespace {

constexpr uint64_t kSessionIdleTimeoutMs = (uint64_t)kSessionIdleTimeoutSeconds * 1000ULL;
constexpr uint64_t kSessionAbsoluteTimeoutMs = (uint64_t)kSessionAbsoluteTimeoutSeconds * 1000ULL;

// Fail-closed on every angle: a record that isn't in_use is "expired"
// (never valid), and a monotonic clock that appears to have gone
// backwards (now_ms before created_at_ms/last_active_at_ms -- impossible
// for hal_time_now_ms()'s own contract, but guarded defensively exactly
// like commissioning_window.cpp's own now_ms < started_at_ms check) is
// treated as expired rather than computing an underflowed, huge "elapsed"
// value that could wrap back into looking valid.
bool is_expired(const SessionRecord& record, uint64_t now_ms) noexcept {
    if (!record.in_use) {
        return true;
    }
    if (now_ms < record.created_at_ms || now_ms < record.last_active_at_ms) {
        return true;
    }
    if ((now_ms - record.created_at_ms) >= kSessionAbsoluteTimeoutMs) {
        return true;
    }
    if ((now_ms - record.last_active_at_ms) >= kSessionIdleTimeoutMs) {
        return true;
    }
    return false;
}

SessionRecord* find_slot(SessionStoreState* state, const char* session_id_hex) noexcept {
    if (state == nullptr || session_id_hex == nullptr || session_id_hex[0] == '\0') {
        return nullptr;
    }
    for (uint32_t i = 0; i < kMaxConcurrentSessions; ++i) {
        SessionRecord& record = state->sessions[i];
        if (record.in_use && std::strcmp(record.session_id_hex, session_id_hex) == 0) {
            return &record;
        }
    }
    return nullptr;
}

}  // namespace

void session_store_sweep_expired(SessionStoreState* state, uint64_t now_ms) noexcept {
    if (state == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < kMaxConcurrentSessions; ++i) {
        SessionRecord& record = state->sessions[i];
        if (record.in_use && is_expired(record, now_ms)) {
            record = SessionRecord{};
        }
    }
}

SessionCreateResult session_store_create(
    SessionStoreState* state, uint64_t now_ms, SessionRecord* out_new_session) noexcept {
    if (state == nullptr || out_new_session == nullptr) {
        return SessionCreateResult::kRngFailed;
    }

    session_store_sweep_expired(state, now_ms);

    SessionRecord* free_slot = nullptr;
    for (uint32_t i = 0; i < kMaxConcurrentSessions; ++i) {
        if (!state->sessions[i].in_use) {
            free_slot = &state->sessions[i];
            break;
        }
    }
    if (free_slot == nullptr) {
        return SessionCreateResult::kFull;
    }

    SessionRecord candidate{};
    if (!generate_random_secret_hex(kSessionIdBytes, candidate.session_id_hex, sizeof(candidate.session_id_hex))) {
        return SessionCreateResult::kRngFailed;
    }
    if (!generate_random_secret_hex(kCsrfTokenBytes, candidate.csrf_token_hex, sizeof(candidate.csrf_token_hex))) {
        return SessionCreateResult::kRngFailed;
    }
    candidate.in_use = true;
    candidate.created_at_ms = now_ms;
    candidate.last_active_at_ms = now_ms;

    *free_slot = candidate;
    *out_new_session = candidate;
    return SessionCreateResult::kCreated;
}

bool session_store_is_valid(const SessionStoreState& state, const char* session_id_hex, uint64_t now_ms) noexcept {
    // find_slot() takes a non-const state pointer only to return a
    // mutable record for touch()/revoke(); this read-only query casts
    // through the same lookup rather than duplicating it.
    const SessionRecord* record = find_slot(const_cast<SessionStoreState*>(&state), session_id_hex);
    return record != nullptr && !is_expired(*record, now_ms);
}

bool session_store_touch(SessionStoreState* state, const char* session_id_hex, uint64_t now_ms) noexcept {
    SessionRecord* record = find_slot(state, session_id_hex);
    if (record == nullptr || is_expired(*record, now_ms)) {
        return false;
    }
    record->last_active_at_ms = now_ms;
    return true;
}

void session_store_revoke(SessionStoreState* state, const char* session_id_hex) noexcept {
    SessionRecord* record = find_slot(state, session_id_hex);
    if (record == nullptr) {
        return;
    }
    *record = SessionRecord{};
}

bool session_store_get_csrf_token(
    const SessionStoreState& state, const char* session_id_hex, uint64_t now_ms, char* out_csrf_token_hex,
    uint32_t out_capacity) noexcept {
    if (out_csrf_token_hex == nullptr || out_capacity < (kCsrfTokenHexChars + 1U)) {
        return false;
    }
    const SessionRecord* record = find_slot(const_cast<SessionStoreState*>(&state), session_id_hex);
    if (record == nullptr || is_expired(*record, now_ms)) {
        return false;
    }
    std::memcpy(out_csrf_token_hex, record->csrf_token_hex, sizeof(record->csrf_token_hex));
    return true;
}

}  // namespace service
