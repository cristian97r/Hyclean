// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 tasarren

// Pieces every DLSS backend (Streamline on Windows, the Lichii DLSS module on
// Linux) shares. The backend files keep only their Streamline/NGX specifics.

#pragma once

#include <atomic>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/vk_dlss_upscaler.h"

namespace Vulkan::Dlss {

constexpr VkFormat OUTPUT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
// Both backends clear the output before DLSS writes it.
constexpr VkImageUsageFlags OUTPUT_USAGE = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;

int EnvInt(const char* name, int fallback);

VkImageMemoryBarrier Barrier(const DlssUpscaler::Image& image, VkAccessFlags src_access,
                             VkAccessFlags dst_access, VkImageLayout old_layout,
                             VkImageLayout new_layout);

/// The caller draws the output this frame even when DLSS writes nothing to it: clear it
/// then, rather than show undefined contents (FSR 3.1 takes over from the next frame).
void ClearOutput(vk::CommandBuffer& cmdbuf, VkImage image);

/// Evaluations that succeeded in this process, for the status bar.
extern std::atomic<u64> evaluations;
/// The last evaluation's mode, requested mode and preset, a byte each.
extern std::atomic<u32> last_evaluation;

} // namespace Vulkan::Dlss
