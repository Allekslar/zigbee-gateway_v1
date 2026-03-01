/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

class ServiceRuntime;

enum class ConnectivityAutoconnectResult : uint8_t {
    kCredentialsMissing = 0,
    kConnectStarted = 1,
    kConnectFailed = 2,
};

class ConnectivityManager {
public:
    ConnectivityAutoconnectResult autoconnect_from_saved_credentials(ServiceRuntime& runtime) noexcept;

    bool has_saved_wifi_credentials() noexcept;
    void mark_wifi_credentials_available() noexcept;

    bool ensure_wifi_mode_for_scan() noexcept;
    bool ensure_wifi_mode_for_sta_connect() noexcept;
    bool start_provisioning_ap(const char* ssid, const char* password) noexcept;

    bool ensure_zigbee_started(ServiceRuntime& runtime) noexcept;
    bool zigbee_started() const noexcept;

    bool can_attempt_autoconnect(uint32_t now_ms, uint8_t max_retries) const noexcept;
    void clear_autoconnect_backoff(ServiceRuntime& runtime) noexcept;

private:
    uint8_t autoconnect_retry_count_{0};
    uint32_t next_autoconnect_attempt_ms_{0};
    bool zigbee_started_{false};
    bool zigbee_start_allowed_{false};
};

}  // namespace service
