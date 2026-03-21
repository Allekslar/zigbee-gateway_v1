/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "device_identity_store.hpp"

#include <algorithm>

namespace service {

bool DeviceIdentityStore::mark_pending(uint16_t short_addr) noexcept {
    DeviceIdentityEntry* entry = find_or_allocate(short_addr);
    if (entry == nullptr) {
        return false;
    }
    if (entry->status == DeviceIdentityStatus::kUnknown) {
        entry->status = DeviceIdentityStatus::kPending;
    }
    return true;
}

bool DeviceIdentityStore::store_manufacturer(
    uint16_t short_addr,
    const char* value,
    std::size_t len) noexcept {
    DeviceIdentityEntry* entry = find_mutable(short_addr);
    if (entry == nullptr) {
        return false;
    }
    if (value == nullptr || len == 0U) {
        entry->manufacturer[0] = '\0';
    } else {
        const std::size_t copy_len = std::min(len, kDeviceIdentityManufacturerMaxLen - 1U);
        std::memcpy(entry->manufacturer.data(), value, copy_len);
        entry->manufacturer[copy_len] = '\0';
    }
    try_resolve(*entry);
    return true;
}

bool DeviceIdentityStore::store_model(
    uint16_t short_addr,
    const char* value,
    std::size_t len) noexcept {
    DeviceIdentityEntry* entry = find_mutable(short_addr);
    if (entry == nullptr) {
        return false;
    }
    if (value == nullptr || len == 0U) {
        entry->model[0] = '\0';
    } else {
        const std::size_t copy_len = std::min(len, kDeviceIdentityModelMaxLen - 1U);
        std::memcpy(entry->model.data(), value, copy_len);
        entry->model[copy_len] = '\0';
    }
    try_resolve(*entry);
    return true;
}

bool DeviceIdentityStore::mark_failed(uint16_t short_addr) noexcept {
    DeviceIdentityEntry* entry = find_mutable(short_addr);
    if (entry == nullptr) {
        return false;
    }
    entry->status = DeviceIdentityStatus::kFailed;
    return true;
}

const DeviceIdentityEntry* DeviceIdentityStore::find(uint16_t short_addr) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.short_addr == short_addr) {
            return &entry;
        }
    }
    return nullptr;
}

bool DeviceIdentityStore::remove(uint16_t short_addr) noexcept {
    for (auto& entry : entries_) {
        if (entry.short_addr == short_addr) {
            entry = DeviceIdentityEntry{};
            return true;
        }
    }
    return false;
}

void DeviceIdentityStore::clear() noexcept {
    for (auto& entry : entries_) {
        entry = DeviceIdentityEntry{};
    }
}

DeviceIdentityEntry* DeviceIdentityStore::find_or_allocate(uint16_t short_addr) noexcept {
    if (short_addr == kUnknownShortAddr) {
        return nullptr;
    }
    for (auto& entry : entries_) {
        if (entry.short_addr == short_addr) {
            return &entry;
        }
    }
    for (auto& entry : entries_) {
        if (entry.short_addr == kUnknownShortAddr) {
            entry.short_addr = short_addr;
            return &entry;
        }
    }
    return nullptr;
}

DeviceIdentityEntry* DeviceIdentityStore::find_mutable(uint16_t short_addr) noexcept {
    for (auto& entry : entries_) {
        if (entry.short_addr == short_addr) {
            return &entry;
        }
    }
    return nullptr;
}

void DeviceIdentityStore::try_resolve(DeviceIdentityEntry& entry) noexcept {
    if (entry.status != DeviceIdentityStatus::kPending) {
        return;
    }
    if (entry.manufacturer[0] != '\0' && entry.model[0] != '\0') {
        entry.status = DeviceIdentityStatus::kResolved;
    }
}

}  // namespace service
