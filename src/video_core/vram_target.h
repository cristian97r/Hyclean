// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "common/settings_enums.h"

namespace VideoCommon {

/// Device memory the emulator aims to stay under for `mode`, given the driver's budget for the
/// process when the device is created (0 without VK_EXT_memory_budget). 0 means no target: the
/// caches follow the driver's budget alone. Auto takes 70% of the budget, leaving the rest to the
/// desktop and other programs. A fixed target above the budget is left as is: the budget binds
/// first. The target is soft: the texture cache collects to stay under it, allocations past it
/// still succeed.
// ponytail: a target is only met by collecting textures and buffers. Under ~3 GB Ocarina of
// Time's working set does not fit at 1x; degrade steps (render scale x0.5, mip bias or dropping
// top mips of large textures) are a follow-up once 2 GB is measured.
[[nodiscard]] constexpr u64 VramTarget(Settings::VramUsageMode mode, u64 device_budget) {
    switch (mode) {
    case Settings::VramUsageMode::Unlimited:
        return 0;
    case Settings::VramUsageMode::Gb2:
        return u64{2} << 30;
    case Settings::VramUsageMode::Gb4:
        return u64{4} << 30;
    case Settings::VramUsageMode::Gb8:
        return u64{8} << 30;
    case Settings::VramUsageMode::Auto:
    default:
        return device_budget / 10 * 7;
    }
}

} // namespace VideoCommon
