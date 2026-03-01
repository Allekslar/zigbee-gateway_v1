/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_nvs.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#include "log_tags.h"
#include "nvs.h"
#include "nvs_flash.h"
#else
#include <stdbool.h>
#endif

static hal_nvs_callbacks_t s_callbacks;
static void* s_context = 0;

#ifdef ESP_PLATFORM
static const char* kTag = LOG_TAG_HAL_NVS;
#endif

#ifndef ESP_PLATFORM
typedef struct {
    bool used;
    char key[32];
    uint32_t value;
} host_u32_entry_t;

typedef struct {
    bool used;
    char key[32];
    char value[129];
} host_str_entry_t;

static host_u32_entry_t s_u32_entries[16];
static host_str_entry_t s_str_entries[16];
static bool s_host_storage_initialized = false;

static host_u32_entry_t* host_find_u32_entry(const char* key, bool allow_create) {
    host_u32_entry_t* free_entry = NULL;
    for (size_t i = 0; i < (sizeof(s_u32_entries) / sizeof(s_u32_entries[0])); ++i) {
        host_u32_entry_t* entry = &s_u32_entries[i];
        if (entry->used) {
            if (strcmp(entry->key, key) == 0) {
                return entry;
            }
            continue;
        }

        if (free_entry == NULL) {
            free_entry = entry;
        }
    }

    if (!allow_create || free_entry == NULL) {
        return NULL;
    }

    memset(free_entry, 0, sizeof(*free_entry));
    strncpy(free_entry->key, key, sizeof(free_entry->key) - 1);
    free_entry->used = true;
    return free_entry;
}

static host_str_entry_t* host_find_str_entry(const char* key, bool allow_create) {
    host_str_entry_t* free_entry = NULL;
    for (size_t i = 0; i < (sizeof(s_str_entries) / sizeof(s_str_entries[0])); ++i) {
        host_str_entry_t* entry = &s_str_entries[i];
        if (entry->used) {
            if (strcmp(entry->key, key) == 0) {
                return entry;
            }
            continue;
        }

        if (free_entry == NULL) {
            free_entry = entry;
        }
    }

    if (!allow_create || free_entry == NULL) {
        return NULL;
    }

    memset(free_entry, 0, sizeof(*free_entry));
    strncpy(free_entry->key, key, sizeof(free_entry->key) - 1);
    free_entry->used = true;
    return free_entry;
}
#endif

hal_nvs_status_t hal_nvs_init(void) {
    s_callbacks = (hal_nvs_callbacks_t){0};
    s_context = 0;

#ifdef ESP_PLATFORM
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "nvs_flash_init returned %s, erasing NVS partition", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "nvs_flash_erase failed: %s", esp_err_to_name(err));
            return HAL_NVS_STATUS_ERR;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_flash_init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(kTag, "nvs_flash_init ok");
    }
    return err == ESP_OK ? HAL_NVS_STATUS_OK : HAL_NVS_STATUS_ERR;
#else
    if (!s_host_storage_initialized) {
        memset(s_u32_entries, 0, sizeof(s_u32_entries));
        memset(s_str_entries, 0, sizeof(s_str_entries));
        s_host_storage_initialized = true;
    }
    return HAL_NVS_STATUS_OK;
#endif
}

hal_nvs_status_t hal_nvs_register_callbacks(const hal_nvs_callbacks_t* callbacks, void* context) {
    if (callbacks == 0) {
        return HAL_NVS_STATUS_INVALID_ARG;
    }

    s_callbacks = *callbacks;
    s_context = context;
    return HAL_NVS_STATUS_OK;
}

hal_nvs_status_t hal_nvs_set_u32(const char* key, uint32_t value) {
    if (key == 0 || key[0] == '\0') {
        return HAL_NVS_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    nvs_handle_t handle = 0;
    if (nvs_open("zigbee_gateway", NVS_READWRITE, &handle) != ESP_OK) {
        return HAL_NVS_STATUS_ERR;
    }

    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        return HAL_NVS_STATUS_ERR;
    }
#else
    host_u32_entry_t* entry = host_find_u32_entry(key, true);
    if (entry == NULL) {
        return HAL_NVS_STATUS_NO_SPACE;
    }

    entry->value = value;
#endif

    if (s_callbacks.on_u32_written != 0) {
        s_callbacks.on_u32_written(s_context, key, value);
    }
    return HAL_NVS_STATUS_OK;
}

hal_nvs_status_t hal_nvs_get_u32(const char* key, uint32_t* value_out) {
    if (key == 0 || value_out == 0) {
        return HAL_NVS_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    nvs_handle_t handle = 0;
    if (nvs_open("zigbee_gateway", NVS_READONLY, &handle) != ESP_OK) {
        return HAL_NVS_STATUS_ERR;
    }

    const esp_err_t err = nvs_get_u32(handle, key, value_out);
    nvs_close(handle);
    if (err == ESP_OK) {
        return HAL_NVS_STATUS_OK;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return HAL_NVS_STATUS_NOT_FOUND;
    }
    return HAL_NVS_STATUS_ERR;
#else
    const host_u32_entry_t* entry = host_find_u32_entry(key, false);
    if (entry == NULL || !entry->used) {
        return HAL_NVS_STATUS_NOT_FOUND;
    }

    *value_out = entry->value;
    return HAL_NVS_STATUS_OK;
#endif
}

hal_nvs_status_t hal_nvs_set_str(const char* key, const char* value) {
    if (key == 0 || key[0] == '\0' || value == 0) {
        return HAL_NVS_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    nvs_handle_t handle = 0;
    if (nvs_open("zigbee_gateway", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(kTag, "set_str open failed, key='%s'", key);
        return HAL_NVS_STATUS_ERR;
    }

    esp_err_t err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "set_str ok, key='%s'", key);
    } else {
        ESP_LOGE(kTag, "set_str failed, key='%s' err=%s", key, esp_err_to_name(err));
    }
    return err == ESP_OK ? HAL_NVS_STATUS_OK : HAL_NVS_STATUS_ERR;
#else
    const size_t value_len = strlen(value);
    host_str_entry_t* entry = host_find_str_entry(key, true);
    if (entry == NULL || value_len + 1 > sizeof(entry->value)) {
        return entry == NULL ? HAL_NVS_STATUS_NO_SPACE : HAL_NVS_STATUS_ERR;
    }

    memcpy(entry->value, value, value_len + 1);
    return HAL_NVS_STATUS_OK;
#endif
}

hal_nvs_status_t hal_nvs_get_str(const char* key, char* value_out, uint32_t value_out_capacity) {
    if (key == 0 || key[0] == '\0' || value_out == 0 || value_out_capacity == 0) {
        return HAL_NVS_STATUS_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    nvs_handle_t handle = 0;
    if (nvs_open("zigbee_gateway", NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGE(kTag, "get_str open failed, key='%s'", key);
        return HAL_NVS_STATUS_ERR;
    }

    size_t required_len = (size_t)value_out_capacity;
    const esp_err_t err = nvs_get_str(handle, key, value_out, &required_len);
    nvs_close(handle);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "get_str ok, key='%s' len=%u", key, (unsigned)required_len);
    } else {
        ESP_LOGW(kTag, "get_str miss/fail, key='%s' err=%s", key, esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        return HAL_NVS_STATUS_OK;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return HAL_NVS_STATUS_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        return HAL_NVS_STATUS_NO_SPACE;
    }
    return HAL_NVS_STATUS_ERR;
#else
    const host_str_entry_t* entry = host_find_str_entry(key, false);
    if (entry == NULL || !entry->used) {
        return HAL_NVS_STATUS_NOT_FOUND;
    }

    const size_t source_len = strlen(entry->value);
    if (source_len + 1 > value_out_capacity) {
        return HAL_NVS_STATUS_NO_SPACE;
    }

    memcpy(value_out, entry->value, source_len + 1);
    return HAL_NVS_STATUS_OK;
#endif
}
