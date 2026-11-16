// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vram_target.h"

namespace {

constexpr u64 MiB = u64{1} << 20;
constexpr u64 GiB = u64{1} << 30;

} // namespace

TEST_CASE("VRAM usage modes map to targets", "[video_core][vram_target]") {
    using Settings::VramUsageMode;
    using VideoCommon::VramTarget;
    // RTX 5080: ~15 GB driver budget.
    constexpr u64 budget = 15 * GiB;
    REQUIRE(VramTarget(VramUsageMode::Auto, budget) == budget / 10 * 7);
    REQUIRE(VramTarget(VramUsageMode::Unlimited, budget) == 0);
    REQUIRE(VramTarget(VramUsageMode::Gb2, budget) == 2 * GiB);
    REQUIRE(VramTarget(VramUsageMode::Gb4, budget) == 4 * GiB);
    REQUIRE(VramTarget(VramUsageMode::Gb8, budget) == 8 * GiB);
    // A 4 GB card: Auto stays under what the driver allows.
    REQUIRE(VramTarget(VramUsageMode::Auto, 3584 * MiB) < 3584 * MiB);
    // No budget known: no target from Auto.
    REQUIRE(VramTarget(VramUsageMode::Auto, 0) == 0);
}

TEST_CASE("Old VRAM usage mode indices load as Auto and Unlimited", "[video_core][vram_target]") {
    // qt-config.ini stores the index: 0 was Conservative, 1 Aggressive.
    REQUIRE(static_cast<u32>(Settings::VramUsageMode::Auto) == 0);
    REQUIRE(static_cast<u32>(Settings::VramUsageMode::Unlimited) == 1);
}
