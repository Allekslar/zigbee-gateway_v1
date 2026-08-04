/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "device_descriptor_store.hpp"

#include <algorithm>

namespace service {

bool DeviceDescriptorStore::mark_pending(uint16_t short_addr) noexcept {
    DeviceDescriptorEntry* entry = find_or_allocate(short_addr);
    if (entry == nullptr) {
        return false;
    }
    if (entry->status == DeviceDescriptorStatus::kUnknown) {
        entry->status = DeviceDescriptorStatus::kPending;
    }
    return true;
}

bool DeviceDescriptorStore::store_manufacturer(
    uint16_t short_addr,
    const char* value,
    std::size_t len) noexcept {
    DeviceDescriptorEntry* entry = find_mutable(short_addr);
    if (entry == nullptr) {
        return false;
    }
    if (value == nullptr || len == 0U) {
        entry->manufacturer[0] = '\0';
    } else {
        const std::size_t copy_len = std::min(len, kDeviceDescriptorManufacturerMaxLen - 1U);
        std::memcpy(entry->manufacturer.data(), value, copy_len);
        entry->manufacturer[copy_len] = '\0';
    }
    try_resolve(*entry);
    return true;
}

bool DeviceDescriptorStore::store_model(
    uint16_t short_addr,
    const char* value,
    std::size_t len) noexcept {
    DeviceDescriptorEntry* entry = find_mutable(short_addr);
    if (entry == nullptr) {
        return false;
    }
    if (value == nullptr || len == 0U) {
        entry->model[0] = '\0';
    } else {
        const std::size_t copy_len = std::min(len, kDeviceDescriptorModelMaxLen - 1U);
        std::memcpy(entry->model.data(), value, copy_len);
        entry->model[copy_len] = '\0';
    }
    try_resolve(*entry);
    return true;
}

bool DeviceDescriptorStore::mark_failed(uint16_t short_addr) noexcept {
    DeviceDescriptorEntry* entry = find_mutable(short_addr);
    if (entry == nullptr) {
        return false;
    }
    entry->status = DeviceDescriptorStatus::kFailed;
    return true;
}

const DeviceDescriptorEntry* DeviceDescriptorStore::find(uint16_t short_addr) const noexcept {
    const auto it = std::find_if(
        entries_.begin(), entries_.end(),
        [short_addr](const DeviceDescriptorEntry& entry) noexcept { return entry.short_addr == short_addr; });
    return it == entries_.end() ? nullptr : &(*it);
}

bool DeviceDescriptorStore::remove(uint16_t short_addr) noexcept {
    const auto it = std::find_if(
        entries_.begin(), entries_.end(),
        [short_addr](const DeviceDescriptorEntry& entry) noexcept { return entry.short_addr == short_addr; });
    if (it == entries_.end()) {
        return false;
    }
    *it = DeviceDescriptorEntry{};
    return true;
}

void DeviceDescriptorStore::clear() noexcept {
    std::fill(entries_.begin(), entries_.end(), DeviceDescriptorEntry{});
}

DeviceDescriptorEntry* DeviceDescriptorStore::find_or_allocate(uint16_t short_addr) noexcept {
    if (short_addr == kUnknownShortAddr) {
        return nullptr;
    }
    const auto existing = std::find_if(
        entries_.begin(), entries_.end(),
        [short_addr](const DeviceDescriptorEntry& entry) noexcept { return entry.short_addr == short_addr; });
    if (existing != entries_.end()) {
        return &(*existing);
    }
    const auto free_slot = std::find_if(
        entries_.begin(), entries_.end(),
        [](const DeviceDescriptorEntry& entry) noexcept { return entry.short_addr == kUnknownShortAddr; });
    if (free_slot == entries_.end()) {
        return nullptr;
    }
    free_slot->short_addr = short_addr;
    return &(*free_slot);
}

DeviceDescriptorEntry* DeviceDescriptorStore::find_mutable(uint16_t short_addr) noexcept {
    const auto it = std::find_if(
        entries_.begin(), entries_.end(),
        [short_addr](const DeviceDescriptorEntry& entry) noexcept { return entry.short_addr == short_addr; });
    return it == entries_.end() ? nullptr : &(*it);
}

void DeviceDescriptorStore::try_resolve(DeviceDescriptorEntry& entry) noexcept {
    if (entry.status != DeviceDescriptorStatus::kPending) {
        return;
    }
    if (entry.manufacturer[0] != '\0' && entry.model[0] != '\0') {
        entry.status = DeviceDescriptorStatus::kResolved;
    }
}

}  // namespace service
