/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

// Plan S6, "Security invariants, bounded tunables and typed accessors"
// (docs/implementation/PRODUCTION_HARDENING_PLAN.md, Stage S6 -- the
// section explicitly ordered "before implementing the control-plane
// changes"): "create the Kconfig definitions and one typed
// security-bounds accessor consumed by provisioning, Web parsing, rate
// limiting and audit storage... The typed accessor is the only
// application-facing source for tunables; handlers/adapters must not
// repeat numeric literals."
//
// This header is that accessor. It does not itself implement
// provisioning, JSON parsing, rate limiting or audit storage -- each of
// those is a separate, later S6 sub-slice that will consume
// `security_bounds()` instead of reading a `CONFIG_ZGW_*` macro directly.
// Nothing in this repository calls `security_bounds()` yet, matching
// every S5 sub-slice's "foundation first" discipline carried into S6.
//
// Ten fields are genuinely configurable (`main/Kconfig.projbuild`'s new
// "Security" submenu, each declared with a real Kconfig `range MIN MAX`
// so out-of-range values are rejected at configure time -- "Kconfig
// rejects out-of-range values" is therefore satisfied by the Kconfig
// declaration itself, not re-implemented here). Two related values are
// explicitly named in the plan text as NOT configurable at all --
// `kProvisioningPassphraseBase32Chars` and the login-backoff
// start/maximum -- and are fixed `constexpr` members instead of Kconfig
// symbols, so there is no menuconfig entry a build could set differently
// even by mistake.
//
// ESP_PLATFORM: `security_bounds()` returns a struct populated once from
// the real generated `CONFIG_ZGW_*` macros (`security_bounds.cpp`).
// Host builds: since Kconfig does not exist on a host build at all, the
// ten fields are populated with the plan's own approved-default values
// (the same defaults declared in Kconfig) -- this lets every later S6
// host-tested component (JSON parsing, rate limiting, audit ring) build
// and test its bounds-respecting logic without a real ESP-IDF sdkconfig,
// matching how `config_manager.hpp`'s own `kDefaultCommandTimeoutMs`
// etc. are already plain C++ constants independent of any Kconfig
// source.
struct SecurityBounds {
    uint32_t commissioning_window_seconds{0};
    uint32_t json_max_body_bytes{0};
    uint32_t json_max_depth{0};
    uint32_t json_max_string_bytes{0};
    uint32_t json_max_keys{0};
    uint32_t login_attempts_per_minute{0};
    uint32_t commands_per_minute{0};
    uint32_t mutations_per_minute{0};
    uint32_t firmware_ops_per_hour{0};
    uint32_t audit_ring_records{0};

    // Fixed, non-configurable (plan S6 text, verbatim): "The provisioning
    // passphrase length remains a non-configurable 16 Base32 characters,
    // and login backoff remains a non-configurable 2-second start with a
    // 60-second maximum."
    static constexpr uint32_t kProvisioningPassphraseBase32Chars = 16U;
    static constexpr uint32_t kLoginBackoffStartSeconds = 2U;
    static constexpr uint32_t kLoginBackoffMaxSeconds = 60U;
};

const SecurityBounds& security_bounds() noexcept;

// Plan S6's own FD-13 range table, exposed so a verifier (or a test) can
// check a *candidate* value against the approved range without
// duplicating the ten (min, max, default) triples anywhere else. Returns
// false for a field name this function does not recognize.
struct SecurityBoundRange {
    uint32_t minimum;
    uint32_t maximum;
    uint32_t approved_default;
};

enum class SecurityBoundField : uint8_t {
    kCommissioningWindowSeconds = 0,
    kJsonMaxBodyBytes = 1,
    kJsonMaxDepth = 2,
    kJsonMaxStringBytes = 3,
    kJsonMaxKeys = 4,
    kLoginAttemptsPerMinute = 5,
    kCommandsPerMinute = 6,
    kMutationsPerMinute = 7,
    kFirmwareOpsPerHour = 8,
    kAuditRingRecords = 9,
};

SecurityBoundRange security_bound_range(SecurityBoundField field) noexcept;

// True if `value` falls within `security_bound_range(field)`'s
// [minimum, maximum] inclusive range -- the same range Kconfig itself
// already enforces at configure time via `range MIN MAX`; exposed as a
// runtime-callable predicate for the production verifier and for tests
// that want to check a value without hand-copying the range table.
bool security_bound_value_in_range(SecurityBoundField field, uint32_t value) noexcept;

}  // namespace service
