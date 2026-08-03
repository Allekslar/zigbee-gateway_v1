/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "service_public_types.hpp"

namespace service {

inline constexpr std::size_t kDeviceDescriptorManufacturerMaxLen = 32U;
inline constexpr std::size_t kDeviceDescriptorModelMaxLen = 32U;

enum class DeviceDescriptorStatus : uint8_t {
    kUnknown = 0,
    kPending = 1,
    kResolved = 2,
    kFailed = 3,
};

struct DeviceDescriptorEntry {
    uint16_t short_addr{kUnknownShortAddr};
    DeviceDescriptorStatus status{DeviceDescriptorStatus::kUnknown};
    std::array<char, kDeviceDescriptorManufacturerMaxLen> manufacturer{};
    std::array<char, kDeviceDescriptorModelMaxLen> model{};
};

class DeviceDescriptorStore {
public:
    bool mark_pending(uint16_t short_addr) noexcept;
    bool store_manufacturer(uint16_t short_addr, const char* value, std::size_t len) noexcept;
    bool store_model(uint16_t short_addr, const char* value, std::size_t len) noexcept;
    bool mark_failed(uint16_t short_addr) noexcept;

    const DeviceDescriptorEntry* find(uint16_t short_addr) const noexcept;
    bool remove(uint16_t short_addr) noexcept;
    void clear() noexcept;

private:
    DeviceDescriptorEntry* find_or_allocate(uint16_t short_addr) noexcept;
    DeviceDescriptorEntry* find_mutable(uint16_t short_addr) noexcept;
    void try_resolve(DeviceDescriptorEntry& entry) noexcept;

    std::array<DeviceDescriptorEntry, kServiceMaxDevices> entries_{};
};

}  // namespace service
