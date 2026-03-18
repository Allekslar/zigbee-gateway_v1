/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ota_bootstrap.hpp"

namespace {

bool s_pending_verify = false;
int s_mark_valid_status = 0;
int s_mark_valid_calls = 0;

}  // namespace

extern "C" bool hal_ota_platform_running_partition_pending_verify(void) {
    return s_pending_verify;
}

extern "C" int hal_ota_platform_mark_running_partition_valid(void) {
    ++s_mark_valid_calls;
    return s_mark_valid_status;
}

extern "C" int hal_ota_platform_schedule_restart(uint32_t delay_ms) {
    (void)delay_ms;
    return 0;
}

extern "C" bool hal_ota_platform_get_running_version(char* out, size_t out_len) {
    static constexpr const char* kVersion = "test-version";
    if (out == nullptr || out_len < std::strlen(kVersion) + 1U) {
        return false;
    }

    std::memcpy(out, kVersion, std::strlen(kVersion) + 1U);
    return true;
}

int main() {
    s_pending_verify = false;
    s_mark_valid_status = 0;
    s_mark_valid_calls = 0;
    assert(service::confirm_pending_ota_image() == service::OtaBootConfirmResult::kNotRequired);
    assert(s_mark_valid_calls == 0);

    s_pending_verify = true;
    s_mark_valid_status = 0;
    s_mark_valid_calls = 0;
    assert(service::confirm_pending_ota_image() == service::OtaBootConfirmResult::kConfirmed);
    assert(s_mark_valid_calls == 1);

    s_pending_verify = true;
    s_mark_valid_status = -1;
    s_mark_valid_calls = 0;
    assert(service::confirm_pending_ota_image() == service::OtaBootConfirmResult::kFailed);
    assert(s_mark_valid_calls == 1);

    return 0;
}
