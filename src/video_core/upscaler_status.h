// SPDX-License-Identifier: GPL-3.0-or-later

// Which temporal upscaler replaces a game's DLSS, for the status bar.

#pragma once

#include <string>

#include "common/common_types.h"
#include "common/settings_enums.h"

namespace VideoCore {

enum class Upscaler : u8 { None, Dlss, Fsr3, Fsr4 };

/// Evaluations so far per temporal upscaler, for the status bar
struct UpscalerStatus {
    u64 dlss{};
    u64 fsr3{};
    u64 fsr4{};
    /// The last DLSS evaluation's settings: the mode it ran in, and the mode and preset the user
    /// chose
    Settings::NvidiaDlssQuality mode{};
    Settings::NvidiaDlssQuality requested{};
    Settings::NvidiaDlssPreset preset{};
    /// Why DLSS is not running though it is ticked; empty when it runs or is off
    std::string dlss_fallback;
};

/// The upscaler that evaluated since the last status: DLSS before FSR 4 before FSR 3.1, as the
/// renderer tries them. None when nothing did (no game upscale this tick).
constexpr Upscaler ActiveUpscaler(const UpscalerStatus& before, const UpscalerStatus& now) {
    if (now.dlss > before.dlss) {
        return Upscaler::Dlss;
    }
    if (now.fsr4 > before.fsr4) {
        return Upscaler::Fsr4;
    }
    if (now.fsr3 > before.fsr3) {
        return Upscaler::Fsr3;
    }
    return Upscaler::None;
}

} // namespace VideoCore
