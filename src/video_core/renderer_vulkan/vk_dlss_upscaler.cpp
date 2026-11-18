// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 tasarren

// Definitions shared by the DLSS backends; see vk_dlss_upscaler_common.h.

#include <cstdlib>

#include "common/settings.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/vk_dlss_upscaler_common.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan::Dlss {

std::atomic<u64> evaluations{};
std::atomic<u32> last_evaluation{};
std::atomic<u64> last_size{};

int EnvInt(const char* name, int fallback) {
    const char* const value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}

VkImageMemoryBarrier Barrier(const DlssUpscaler::Image& image, VkAccessFlags src_access,
                             VkAccessFlags dst_access, VkImageLayout old_layout,
                             VkImageLayout new_layout) {
    VkImageSubresourceRange range = image.range;
    range.aspectMask = image.barrier_aspect;
    return VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange = range,
    };
}

void ClearOutput(vk::CommandBuffer& cmdbuf, VkImage image) {
    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    const auto barrier = [&](VkAccessFlags src_access, VkAccessFlags dst_access) {
        return VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = src_access,
            .dstAccessMask = dst_access,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
    };
    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                           barrier(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_WRITE_BIT));
    cmdbuf.ClearColorImage(image, VK_IMAGE_LAYOUT_GENERAL, VkClearColorValue{}, range);
    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                           barrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
}

} // namespace Vulkan::Dlss

namespace Vulkan {

void DlssUpscaler::CreateOutput(VkExtent2D upscale_size) {
    if (output_image) {
        scheduler.Finish(); // the old output may still be in use
    }
    output_image = memory_allocator.CreateImage(VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = Dlss::OUTPUT_FORMAT,
        .extent = {upscale_size.width, upscale_size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = Dlss::OUTPUT_USAGE,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });
    output_view = device.GetLogical().CreateImageView(VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = *output_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = Dlss::OUTPUT_FORMAT,
        .components{},
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    });
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([image = *output_image](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                               VkImageMemoryBarrier{
                                   .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                   .pNext = nullptr,
                                   .srcAccessMask = 0,
                                   .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                   .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = image,
                                   .subresourceRange{
                                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .baseMipLevel = 0,
                                       .levelCount = 1,
                                       .baseArrayLayer = 0,
                                       .layerCount = 1,
                                   },
                               });
    });
}

void DlssUpscaler::Status(VideoCore::UpscalerStatus& status) {
    status.dlss = Dlss::evaluations.load(std::memory_order_acquire);
    const u32 last = Dlss::last_evaluation.load(std::memory_order_relaxed);
    status.mode = static_cast<Settings::NvidiaDlssQuality>(last & 0xff);
    status.requested = static_cast<Settings::NvidiaDlssQuality>((last >> 8) & 0xff);
    status.preset = static_cast<Settings::NvidiaDlssPreset>((last >> 16) & 0xff);
    status.dlss_size =
        VideoCore::UpscaleSize::Unpack(Dlss::last_size.load(std::memory_order_relaxed));
}

} // namespace Vulkan
