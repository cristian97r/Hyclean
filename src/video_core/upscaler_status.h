// SPDX-License-Identifier: GPL-3.0-or-later

// Which temporal upscaler replaces a game's DLSS, for the status bar.

#pragma once

#include <string>

#include "common/common_types.h"
#include "common/settings_enums.h"

namespace VideoCore {

enum class Upscaler : u8 { None, Dlss, Fsr3, Fsr4 };

/// An evaluation's input and output size, packed in one word so an atomic holds it.
struct UpscaleSize {
    u32 render_width{}, render_height{}, output_width{}, output_height{};

    [[nodiscard]] constexpr u64 Pack() const noexcept {
        return u64{render_width & 0xffff} | (u64{render_height & 0xffff} << 16) |
               (u64{output_width & 0xffff} << 32) | (u64{output_height & 0xffff} << 48);
    }
    [[nodiscard]] static constexpr UpscaleSize Unpack(u64 word) noexcept {
        return {static_cast<u32>(word & 0xffff), static_cast<u32>((word >> 16) & 0xffff),
                static_cast<u32>((word >> 32) & 0xffff), static_cast<u32>(word >> 48)};
    }
    bool operator==(const UpscaleSize&) const = default;
};

/// Evaluations so far per temporal upscaler, for the status bar
struct UpscalerStatus {
    u64 dlss{};
    u64 fsr3{};
    u64 fsr4{};
    /// Each upscaler's last input and output size
    UpscaleSize dlss_size, fsr3_size, fsr4_size;
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

/// The running upscaler's last input and output size.
constexpr UpscaleSize RunningUpscaleSize(const UpscalerStatus& status, Upscaler running) {
    switch (running) {
    case Upscaler::Dlss:
        return status.dlss_size;
    case Upscaler::Fsr3:
        return status.fsr3_size;
    case Upscaler::Fsr4:
        return status.fsr4_size;
    case Upscaler::None:
        break;
    }
    return {};
}

} // namespace VideoCore
