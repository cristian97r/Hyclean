// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 tasarren

#pragma once

#include <string>

#include "common/common_types.h"

namespace Vulkan {

class Device;

// Loads the Lichii DLSS module (externals/lichii-dlss, MIT) and brings up
// NVIDIA NGX on it. The module owns every NGX SDK type; this side only sees
// its C ABI (externals/lichii-dlss/include/ngx_module.h).
namespace Ngx {

// Result of probing or initializing the NGX backend.
enum class Setup {
    Ready,        // Module and driver are usable (or an init already succeeded).
    NotInstalled, // No module .so (or the NVIDIA runtime file) in the NGX dir.
    WrongAbi,     // Module loaded, but its ABI version does not match this build.
    NoNvidia,     // libnvidia-ngx.so.1 is missing: no NVIDIA driver with NGX.
    InitFailed,   // NGX init or the DLSS feature check failed.
};

// What the settings panel shows. Filled without a Device (disk, dlopen,
// ABI, driver library and recorded versions only), so the panel never has
// to boot a game backend.
struct State {
    Setup setup{};
    std::string detail;    // InitFailed reason; version/abi info otherwise.
    std::string module_version;
    std::string dlss_version;
    u32 min_driver_major{};
    u32 min_driver_minor{};
};

// Where the module is looked up: the NGX folder in user data, or the
// EDEN_NX2_NGX_DIR override.
std::string ModuleDir();

// Disk + dlopen probe for the panel. Never touches Vulkan state.
State Probe();

// One NGX init per device (tried again for the next game's). Records why
// it failed for the panel. Safe to call concurrently; cheap after success.
bool EnsureInit(const Device& device);

// Releases NGX after the last DLSS feature is released. Call from the
// upscaler destructor only, with the GPU idle.
void Shutdown();

// Thin pass-throughs to the loaded module, for the upscaler. The command
// buffers are VkCommandBuffer; the pointers are the module ABI structs.
int OptimalSettings(u32 output_width, u32 output_height, int quality, u32* opt_width,
                    u32* opt_height, u32* min_width, u32* min_height, u32* max_width,
                    u32* max_height, float* sharpness);
u64 CreateFeature(void* command_buffer, const void* create);
void ReleaseFeature(u64 feature);
int EvaluateFeature(u64 feature, void* command_buffer, const void* eval);
int Stats(u64* vram_bytes);

// The module's last error text, or an empty string.
std::string LastError();

} // namespace Ngx

} // namespace Vulkan
