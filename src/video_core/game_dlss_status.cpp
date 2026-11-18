// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>

#include "video_core/game_dlss_status.h"

namespace VideoCore {
namespace {
std::atomic<u64> last_status{};
}

void PublishGameDlssStatus(const GameDlssStatus& status) {
    last_status.store(u64{static_cast<u8>(status.state)} |
                          (u64{static_cast<u8>(status.reason)} << 8) |
                          (u64{status.rejected_passes} << 32),
                      std::memory_order_relaxed);
}

GameDlssStatus LastGameDlssStatus() {
    const u64 word = last_status.load(std::memory_order_relaxed);
    return {.state = static_cast<GameDlssState>(word & 0xff),
            .reason = static_cast<GameDlssReason>((word >> 8) & 0xff),
            .rejected_passes = static_cast<u32>(word >> 32)};
}

const char* GameDlssReasonText(GameDlssReason reason) {
    switch (reason) {
    case GameDlssReason::None:
        return "";
    case GameDlssReason::TurnedOff:
        return "NVIDIA DLSS or AMD FSR is chosen in its place";
    case GameDlssReason::NvidiaDlss:
        return "NVIDIA DLSS is turned on and runs in its place";
    case GameDlssReason::ResolutionScaling:
        return "it only makes the game's own resolution (resolution scaling is on)";
    case GameDlssReason::Unsupported:
        return "it needs Vulkan and compute shaders 32 threads wide";
    case GameDlssReason::PassesCannotRun:
        return "some of its passes can't run here";
    case GameDlssReason::InvalidOutput:
        return "its output had invalid values (NaN)";
    case GameDlssReason::OutputMismatch:
        return "its output stopped matching the game's image (frozen or corrupted)";
    }
    return "";
}

} // namespace VideoCore
