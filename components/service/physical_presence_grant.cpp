/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "physical_presence_grant.hpp"

#include <cstring>

namespace service {

namespace {

constexpr uint64_t kPhysicalPresenceGrantMaxLifetimeMs =
    static_cast<uint64_t>(kPhysicalPresenceGrantMaxLifetimeSeconds) * 1000ULL;

// Both null/empty -> match (a grant deliberately not tied to any
// session). Exactly one null/empty -> no match. Otherwise exact,
// case-sensitive string compare.
bool session_id_matches(const char* bound_session_id_hex, const char* candidate_session_id_hex) noexcept {
    const bool bound_empty = (bound_session_id_hex == nullptr || bound_session_id_hex[0] == '\0');
    const bool candidate_empty = (candidate_session_id_hex == nullptr || candidate_session_id_hex[0] == '\0');
    if (bound_empty && candidate_empty) {
        return true;
    }
    if (bound_empty != candidate_empty) {
        return false;
    }
    return std::strcmp(bound_session_id_hex, candidate_session_id_hex) == 0;
}

bool is_valid_locked(
    const PhysicalPresenceGrantState& state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept {
    if (!state.active) {
        return false;
    }
    // Fail closed on an apparently-backwards monotonic clock, the same
    // defensive convention session_store.cpp's own is_expired() and
    // commissioning_window.cpp's own now_ms < started_at_ms check both
    // already establish -- never compute an underflowed "elapsed" value.
    if (now_ms < state.created_at_ms) {
        return false;
    }
    if ((now_ms - state.created_at_ms) >= kPhysicalPresenceGrantMaxLifetimeMs) {
        return false;
    }
    if (!state.ambient) {
        if (state.action_class != action_class) {
            return false;
        }
        if (!session_id_matches(state.session_id_hex, session_id_hex)) {
            return false;
        }
    }
    return true;
}

}  // namespace

void physical_presence_grant_create(
    PhysicalPresenceGrantState* state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept {
    if (state == nullptr) {
        return;
    }

    *state = PhysicalPresenceGrantState{};
    state->created_at_ms = now_ms;
    state->action_class = action_class;
    if (session_id_hex != nullptr) {
        std::strncpy(state->session_id_hex, session_id_hex, kSessionIdHexChars);
        state->session_id_hex[kSessionIdHexChars] = '\0';
    }
    // Set last, once every other field already holds its real value --
    // this is the single field that "commits" the grant to a concurrent
    // reader (the real production caller of physical_presence_grant_
    // create_from_button() is a dedicated FreeRTOS button-polling task,
    // genuinely concurrent with whichever task is handling an HTTP
    // request and calling physical_presence_grant_is_valid()/_consume()
    // at the same time -- unlike every other module in this file's own
    // family, e.g. session_store.cpp, which only this project's single
    // httpd worker task ever touches). Ordering `active` last means any
    // racing reader observes either the fully-old grant (active already
    // false, or a fully-populated previous grant) or the fully-new one,
    // never a torn mix -- and if a race is ever unlucky enough to land
    // exactly on this one-instruction commit point, the only possible
    // observable outcomes are "not active yet" (safe, fails closed) or
    // "active with fully-correct fields" (correct) -- never "active with
    // stale fields", which is the only unsafe outcome worth preventing
    // and cannot occur with this ordering on this project's single-core
    // target (a preempted task always resumes exactly where it left off;
    // no third task exists to observe an intermediate state twice).
    state->active = true;
}

void physical_presence_grant_create_from_button(PhysicalPresenceGrantState* state, uint64_t now_ms) noexcept {
    if (state == nullptr) {
        return;
    }

    *state = PhysicalPresenceGrantState{};
    state->created_at_ms = now_ms;
    state->ambient = true;
    // active set last -- see physical_presence_grant_create()'s own
    // comment for why this ordering matters here specifically.
    state->active = true;
}

bool physical_presence_grant_is_valid(
    const PhysicalPresenceGrantState& state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept {
    return is_valid_locked(state, action_class, session_id_hex, now_ms);
}

bool physical_presence_grant_consume(
    PhysicalPresenceGrantState* state, PhysicalPresenceActionClass action_class, const char* session_id_hex,
    uint64_t now_ms) noexcept {
    if (state == nullptr || !is_valid_locked(*state, action_class, session_id_hex, now_ms)) {
        return false;
    }
    state->active = false;
    return true;
}

}  // namespace service
