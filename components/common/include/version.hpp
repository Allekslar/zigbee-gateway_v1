/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

namespace common {

inline constexpr const char* kProjectName = "zigbee-gateway";
inline constexpr const char* kVersion = "0.1.0";
inline constexpr const char* kBoardId = "zigbee-gateway-esp32c6";

#if defined(CONFIG_IDF_TARGET_ESP32C6)
inline constexpr const char* kChipTarget = "esp32c6";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
inline constexpr const char* kChipTarget = "esp32s3";
#elif defined(ESP_PLATFORM)
inline constexpr const char* kChipTarget = "esp";
#else
inline constexpr const char* kChipTarget = "host";
#endif

}  // namespace common
