/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "tuya_dp_parser.hpp"
#include "tuya_fingerprint.hpp"

namespace service {

inline constexpr std::size_t kTuyaPluginMaxOutputs = 4U;
inline constexpr std::size_t kTuyaDpCommandValueMaxLen = 4U;

enum class TuyaNormalizedKind : uint8_t {
    kNone = 0,
    kTemperatureCentiC,
    kContactOpen,
    kOccupancy,
    kBatteryPercent,
    kPowerOn,
};

struct TuyaNormalizedOutput {
    TuyaNormalizedKind kind{TuyaNormalizedKind::kNone};
    int32_t value{0};
    bool valid{true};
};

struct TuyaPluginResult {
    bool handled{false};
    uint8_t output_count{0};
    TuyaNormalizedOutput outputs[kTuyaPluginMaxOutputs]{};

    bool add(TuyaNormalizedKind kind, int32_t value, bool valid = true) noexcept {
        if (output_count >= kTuyaPluginMaxOutputs) {
            return false;
        }
        outputs[output_count].kind = kind;
        outputs[output_count].value = value;
        outputs[output_count].valid = valid;
        ++output_count;
        return true;
    }
};

struct TuyaCommandRequest {
    TuyaNormalizedKind kind{TuyaNormalizedKind::kNone};
    int32_t value{0};
};

struct TuyaDpCommand {
    bool supported{false};
    uint8_t dp_id{0};
    TuyaDpType dp_type{TuyaDpType::kRaw};
    uint8_t value[kTuyaDpCommandValueMaxLen]{};
    uint8_t value_len{0};
    uint8_t endpoint{1};
};

class TuyaPlugin {
public:
    virtual ~TuyaPlugin() = default;

    virtual bool matches(const TuyaFingerprint& fingerprint) const noexcept = 0;

    virtual TuyaPluginResult translate(
        const TuyaFingerprint& fingerprint,
        const TuyaDpParseResult& dp_result) const noexcept = 0;

    virtual TuyaDpCommand encode_command(
        const TuyaFingerprint& fingerprint,
        const TuyaCommandRequest& request) const noexcept {
        (void)fingerprint;
        (void)request;
        return TuyaDpCommand{};
    }
};

}  // namespace service
