/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

class ConfigManager {
public:
    static constexpr uint32_t kCurrentSchemaVersion = 2;
    static constexpr uint32_t kDefaultCommandTimeoutMs = 5000;
    static constexpr uint8_t kDefaultMaxCommandRetries = 1;
    static constexpr uint8_t kMaxCommandRetries = 5;

    bool load() noexcept;
    bool save() noexcept;
    uint32_t schema_version() const noexcept;
    bool set_command_timeout_ms(uint32_t timeout_ms) noexcept;
    uint32_t command_timeout_ms() const noexcept;
    bool set_max_command_retries(uint8_t retries) noexcept;
    uint8_t max_command_retries() const noexcept;
    bool dirty() const noexcept;

private:
    bool migrate_to_current(uint32_t from_version) noexcept;
    void load_current_values() noexcept;

    uint32_t schema_version_{kCurrentSchemaVersion};
    uint32_t command_timeout_ms_{kDefaultCommandTimeoutMs};
    uint8_t max_command_retries_{kDefaultMaxCommandRetries};
    bool dirty_{false};
};

}  // namespace service
