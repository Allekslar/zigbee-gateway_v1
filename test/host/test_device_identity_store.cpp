/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "device_identity_store.hpp"

int main() {
    service::DeviceIdentityStore store{};

    /* Empty store: find returns nullptr. */
    assert(store.find(0x1234U) == nullptr);

    /* Mark pending: creates entry with kPending status. */
    assert(store.mark_pending(0x1234U));
    const service::DeviceIdentityEntry* entry = store.find(0x1234U);
    assert(entry != nullptr);
    assert(entry->short_addr == 0x1234U);
    assert(entry->status == service::DeviceIdentityStatus::kPending);
    assert(entry->manufacturer[0] == '\0');
    assert(entry->model[0] == '\0');

    /* Store manufacturer only: status stays kPending. */
    assert(store.store_manufacturer(0x1234U, "_TZ3000_abc", 11));
    entry = store.find(0x1234U);
    assert(entry != nullptr);
    assert(entry->status == service::DeviceIdentityStatus::kPending);
    assert(std::strcmp(entry->manufacturer.data(), "_TZ3000_abc") == 0);

    /* Store model: both present → auto-resolves to kResolved. */
    assert(store.store_model(0x1234U, "TS0001", 6));
    entry = store.find(0x1234U);
    assert(entry != nullptr);
    assert(entry->status == service::DeviceIdentityStatus::kResolved);
    assert(std::strcmp(entry->manufacturer.data(), "_TZ3000_abc") == 0);
    assert(std::strcmp(entry->model.data(), "TS0001") == 0);

    /* Mark failed overrides resolved status. */
    assert(store.mark_failed(0x1234U));
    entry = store.find(0x1234U);
    assert(entry != nullptr);
    assert(entry->status == service::DeviceIdentityStatus::kFailed);

    /* Remove entry. */
    assert(store.remove(0x1234U));
    assert(store.find(0x1234U) == nullptr);
    assert(!store.remove(0x1234U));

    /* Store model first, then manufacturer → also auto-resolves. */
    assert(store.mark_pending(0x5678U));
    assert(store.store_model(0x5678U, "TS0201", 6));
    entry = store.find(0x5678U);
    assert(entry != nullptr);
    assert(entry->status == service::DeviceIdentityStatus::kPending);
    assert(store.store_manufacturer(0x5678U, "_TZE200_xyz", 11));
    entry = store.find(0x5678U);
    assert(entry != nullptr);
    assert(entry->status == service::DeviceIdentityStatus::kResolved);

    /* Invalid short_addr rejected. */
    assert(!store.mark_pending(service::kUnknownShortAddr));

    /* Storing to unknown address returns false. */
    assert(!store.store_manufacturer(0x9999U, "test", 4));
    assert(!store.store_model(0x9999U, "test", 4));
    assert(!store.mark_failed(0x9999U));

    /* Clear removes all entries. */
    store.clear();
    assert(store.find(0x5678U) == nullptr);

    /* String truncation: values longer than max are truncated safely. */
    assert(store.mark_pending(0xAAAAU));
    char long_manufacturer[64]{};
    std::memset(long_manufacturer, 'X', sizeof(long_manufacturer) - 1);
    assert(store.store_manufacturer(0xAAAAU, long_manufacturer, sizeof(long_manufacturer) - 1));
    entry = store.find(0xAAAAU);
    assert(entry != nullptr);
    assert(std::strlen(entry->manufacturer.data()) == service::kDeviceIdentityManufacturerMaxLen - 1);

    return 0;
}
