/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "service_public_types.hpp"

namespace service {

inline constexpr std::size_t kDeviceIdentityManufacturerMaxLen = 32U;
inline constexpr std::size_t kDeviceIdentityModelMaxLen = 32U;

enum class DeviceIdentityStatus : uint8_t {
    kUnknown = 0,
    kPending = 1,
    kResolved = 2,
    kFailed = 3,
};

struct DeviceIdentityEntry {
    uint16_t short_addr{kUnknownShortAddr};
    DeviceIdentityStatus status{DeviceIdentityStatus::kUnknown};
    std::array<char, kDeviceIdentityManufacturerMaxLen> manufacturer{};
    std::array<char, kDeviceIdentityModelMaxLen> model{};
};

class DeviceIdentityStore {
public:
    bool mark_pending(uint16_t short_addr) noexcept;
    bool store_manufacturer(uint16_t short_addr, const char* value, std::size_t len) noexcept;
    bool store_model(uint16_t short_addr, const char* value, std::size_t len) noexcept;
    bool mark_failed(uint16_t short_addr) noexcept;

    const DeviceIdentityEntry* find(uint16_t short_addr) const noexcept;
    bool remove(uint16_t short_addr) noexcept;
    void clear() noexcept;

private:
    DeviceIdentityEntry* find_or_allocate(uint16_t short_addr) noexcept;
    DeviceIdentityEntry* find_mutable(uint16_t short_addr) noexcept;
    void try_resolve(DeviceIdentityEntry& entry) noexcept;

    std::array<DeviceIdentityEntry, kServiceMaxDevices> entries_{};
};

}  // namespace service
