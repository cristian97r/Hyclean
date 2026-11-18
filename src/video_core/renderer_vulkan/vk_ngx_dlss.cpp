// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 tasarren

// NVIDIA DLSS (through the Lichii DLSS module and NVIDIA NGX) in place of a
// game's DLSS evaluation, on Linux. The module is externals/lichii-dlss
// (MIT); the NVIDIA files it uses are fetched into the user data's ngx
// folder by the settings panel.

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "ngx_module.h"
#include "video_core/dlss_evaluation.h"
#include "video_core/dlss_mode.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/vk_dlss_upscaler.h"
#include "video_core/renderer_vulkan/vk_dlss_upscaler_common.h"
#include "video_core/renderer_vulkan/vk_ngx_dlss.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan::Ngx {
namespace {

constexpr std::string_view MODULE_FILE = "lichii-dlss-module.so";
constexpr std::string_view RUNTIME_PREFIX = "libnvidia-ngx-dlss.so.";

/// The module's function table. Valid only while module_loaded holds.
struct Module {
    void* library{};
    u32 (*abi)();
    const char* (*version)();
    const char* (*dlss_version)();
    const char* (*last_error)();
    int (*init)(const ngx_module_init_params*);
    void (*shutdown)();
    int (*caps)(ngx_module_caps_info*);
    int (*optimal_settings)(u32, u32, int, u32*, u32*, u32*, u32*, u32*, u32*, float*);
    u64 (*create)(u64, const ngx_module_feature_create*);
    int (*release)(u64);
    int (*eval)(u64, u64, const ngx_module_eval_params*);
    int (*stats)(u64*);
};

std::mutex mutex;
Module module;        ///< The loaded module, once LoadModule succeeds.
bool module_loaded{}; ///< dlopen, symbols and ABI all check out.

bool ngx_inited{};          ///< ngx_module_init succeeded for attempted_device.
bool init_attempted{};      ///< An EnsureInit ran for attempted_device.
VkDevice attempted_device{}; ///< The VkDevice NGX was last tried on.
Setup init_setup{Setup::NotInstalled};
std::string init_detail;
ngx_module_caps_info caps{};

// Version strings copied while the lock is held, so the panel can show them
// after the module is gone.
std::string module_version_copy;
std::string dlss_version_copy;

int fd_alias = -1; ///< Holds the data dir open when its path is not ASCII.

int EnvInt(const char* name, int fallback) {
    const char* const value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}

void NgxLog(const char* message, int level) {
    std::string_view text{message};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    switch (level) {
    case 3:
        LOG_INFO(Render_Vulkan, "NGX: {}", text);
        break;
    case 4:
        LOG_WARNING(Render_Vulkan, "NGX: {}", text);
        break;
    case 5:
        LOG_ERROR(Render_Vulkan, "NGX: {}", text);
        break;
    default:
        LOG_DEBUG(Render_Vulkan, "NGX: {}", text);
        break;
    }
}

enum class LoadResult { Ok, Missing, OpenFailed, WrongAbi, Symbols };

std::string ModuleDirImpl() {
    if (const char* const dir = std::getenv("EDEN_NX2_NGX_DIR"); dir && *dir) {
        return dir;
    }
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NgxDir).string();
}

LoadResult LoadModule(std::string& detail) {
    if (module_loaded) {
        return LoadResult::Ok;
    }
    module = Module{};
    const std::filesystem::path path = std::filesystem::path{ModuleDirImpl()} / MODULE_FILE;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return LoadResult::Missing;
    }
    module.library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module.library) {
        const char* const error = dlerror();
        detail = error ? error : "dlopen failed";
        return LoadResult::OpenFailed;
    }
    const auto load = [&]<typename T>(T& target, const char* name) {
        target = reinterpret_cast<T>(dlsym(module.library, name));
        return target != nullptr;
    };
    if (!load(module.abi, "ngx_module_abi") || !load(module.version, "ngx_module_version") ||
        !load(module.dlss_version, "ngx_module_dlss_version") ||
        !load(module.last_error, "ngx_module_last_error") ||
        !load(module.init, "ngx_module_init") || !load(module.shutdown, "ngx_module_shutdown") ||
        !load(module.caps, "ngx_module_caps") ||
        !load(module.optimal_settings, "ngx_module_optimal_settings") ||
        !load(module.create, "ngx_module_create") || !load(module.release, "ngx_module_release") ||
        !load(module.eval, "ngx_module_eval") || !load(module.stats, "ngx_module_stats")) {
        detail = "the DLSS module is missing an entry point";
        dlclose(module.library);
        module = Module{};
        return LoadResult::Symbols;
    }
    if (const u32 abi = module.abi(); abi != NGX_MODULE_ABI) {
        detail = fmt::format("module ABI {} does not match this Lichii's ABI {}", abi,
                              NGX_MODULE_ABI);
        dlclose(module.library);
        module = Module{};
        return LoadResult::WrongAbi;
    }
    module_loaded = true;
    return LoadResult::Ok;
}

bool DriverPresent() {
    if (void* const core = dlopen("libnvidia-ngx.so.1", RTLD_LAZY | RTLD_LOCAL)) {
        dlclose(core);
        return true;
    }
    return false;
}

bool RuntimePresent(std::string& detail) {
    // The model file the module was built for: libnvidia-ngx-dlss.so.<version>.
    const std::string name = std::string{RUNTIME_PREFIX} + module.dlss_version();
    const std::filesystem::path path = std::filesystem::path{ModuleDirImpl()} / name;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        return true;
    }
    detail = fmt::format("{} is missing", name);
    return false;
}

/// NGX widens the path with the C locale, so a non-ASCII dir must be handed
/// over as a /proc/self/fd alias. The fd stays open for the process's life.
std::string DataPath() {
    const std::string dir = ModuleDirImpl();
    for (const char c : dir) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            if (fd_alias < 0) {
                fd_alias = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            }
            if (fd_alias >= 0) {
                return fmt::format("/proc/self/fd/{}", fd_alias);
            }
            return dir; // no alias possible; NGX will say what it thinks of it
        }
    }
    return dir;
}

void FillVersions(State& state) {
    if (module_loaded) {
        state.module_version = module.version();
        state.dlss_version = module.dlss_version();
    } else {
        state.module_version = module_version_copy;
        state.dlss_version = dlss_version_copy;
    }
    state.min_driver_major = caps.min_driver_major;
    state.min_driver_minor = caps.min_driver_minor;
}

void RecordInitAttempt(Setup setup, std::string detail) {
    init_setup = setup;
    init_detail = std::move(detail);
    if (module_loaded) {
        module_version_copy = module.version();
        dlss_version_copy = module.dlss_version();
    }
}

} // Anonymous namespace

std::string ModuleDir() {
    return ModuleDirImpl();
}

State Probe() {
    std::scoped_lock lock{mutex};
    State state;
    if (init_attempted || ngx_inited) {
        FillVersions(state);
        state.setup = init_setup;
        state.detail = init_detail;
        return state;
    }
    std::string detail;
    switch (LoadModule(detail)) {
    case LoadResult::Ok:
        break;
    case LoadResult::Missing:
        state.setup = Setup::NotInstalled;
        return state;
    case LoadResult::WrongAbi:
        state.setup = Setup::WrongAbi;
        state.detail = detail;
        return state;
    default:
        state.setup = Setup::InitFailed;
        state.detail = detail;
        return state;
    }
    FillVersions(state);
    if (!DriverPresent()) {
        state.setup = Setup::NoNvidia;
        return state;
    }
    if (!RuntimePresent(detail)) {
        state.setup = Setup::NotInstalled;
        state.detail = detail;
        return state;
    }
    state.setup = Setup::Ready;
    return state;
}

bool EnsureInit(const Device& device) {
    std::scoped_lock lock{mutex};
    if (ngx_inited) {
        if (attempted_device == *device.GetLogical()) {
            return true;
        }
        // A new device: a new game. Bring NGX up on it.
        module.shutdown();
        ngx_inited = false;
    } else if (init_attempted && attempted_device == *device.GetLogical()) {
        return false; // already tried for this device
    }
    attempted_device = *device.GetLogical();
    init_attempted = true;

    std::string detail;
    switch (LoadModule(detail)) {
    case LoadResult::Ok:
        break;
    case LoadResult::Missing:
        RecordInitAttempt(Setup::NotInstalled, "the DLSS module is missing");
        return false;
    case LoadResult::WrongAbi:
        RecordInitAttempt(Setup::WrongAbi, detail);
        return false;
    default:
        RecordInitAttempt(Setup::InitFailed, detail);
        return false;
    }
    if (!DriverPresent()) {
        RecordInitAttempt(Setup::NoNvidia, {});
        return false;
    }
    if (!RuntimePresent(detail)) {
        RecordInitAttempt(Setup::NotInstalled, detail);
        return false;
    }

    const std::string data_path = DataPath();
    const ngx_module_init_params init{
        .instance = reinterpret_cast<u64>(device.GetInstance()),
        .physical_device = reinterpret_cast<u64>(static_cast<VkPhysicalDevice>(device.GetPhysical())),
        .device = reinterpret_cast<u64>(*device.GetLogical()),
        .get_instance_proc_addr =
            reinterpret_cast<void*>(device.GetDispatchLoader().vkGetInstanceProcAddr),
        .get_device_proc_addr =
            reinterpret_cast<void*>(device.GetDispatchLoader().vkGetDeviceProcAddr),
        .data_path = data_path.c_str(),
        .log = EnvInt("EDEN_NX2_NGX_LOG", 0) != 0 ? NgxLog : nullptr,
    };
    const int status = module.init(&init);
    if (status != NGX_MODULE_OK) {
        switch (status) {
        case NGX_MODULE_NO_NVIDIA:
            RecordInitAttempt(Setup::NoNvidia, {});
            break;
        case NGX_MODULE_OUT_OF_MEMORY:
            RecordInitAttempt(Setup::InitFailed, "out of memory");
            break;
        default:
            RecordInitAttempt(Setup::InitFailed,
                              module.last_error() ? module.last_error() : "NGX init failed");
            break;
        }
        return false;
    }

    if (module.caps(&caps) != NGX_MODULE_OK) {
        caps = ngx_module_caps_info{};
    }
    if (caps.needs_updated_driver) {
        RecordInitAttempt(Setup::InitFailed,
                          fmt::format("driver too old, {}.{} or newer is needed",
                                      caps.min_driver_major, caps.min_driver_minor));
        module.shutdown(); // undo the init so a later attempt can retry
        return false;
    }
    if (!caps.available) {
        RecordInitAttempt(Setup::InitFailed, fmt::format("NGX error {:#x}", caps.feature_init_result));
        module.shutdown();
        return false;
    }

    ngx_inited = true;
    RecordInitAttempt(Setup::Ready, {});
    LOG_INFO(Render_Vulkan, "NVIDIA DLSS (NGX): module {}, DLSS model {} ready",
             module.version(), module.dlss_version());
    return true;
}

void Shutdown() {
    std::scoped_lock lock{mutex};
    if (module_loaded && ngx_inited) {
        module.shutdown();
    }
    ngx_inited = false;
    init_attempted = false;
    attempted_device = nullptr;
}

int OptimalSettings(u32 output_width, u32 output_height, int quality, u32* opt_width,
                    u32* opt_height, u32* min_width, u32* min_height, u32* max_width,
                    u32* max_height, float* sharpness) {
    return module.optimal_settings(output_width, output_height, quality, opt_width, opt_height,
                                   min_width, min_height, max_width, max_height, sharpness);
}

u64 CreateFeature(void* command_buffer, const void* create) {
    return module.create(reinterpret_cast<u64>(command_buffer),
                          static_cast<const ngx_module_feature_create*>(create));
}

void ReleaseFeature(u64 feature) {
    if (feature != 0) {
        module.release(feature);
    }
}

int EvaluateFeature(u64 feature, void* command_buffer, const void* eval) {
    return module.eval(feature, reinterpret_cast<u64>(command_buffer),
                       static_cast<const ngx_module_eval_params*>(eval));
}

int Stats(u64* vram_bytes) {
    return module.stats(vram_bytes);
}

std::string LastError() {
    return module_loaded && module.last_error ? module.last_error() : std::string{};
}

} // namespace Vulkan::Ngx

namespace Vulkan {
namespace {

// NGX gives a feature one chance to be recreated before Lichii falls back.
constexpr u32 EVAL_FAILURE_LIMIT = 30;

/// NGX perf-quality values; Automatic has no NGX mode of its own.
int NgxQualityFor(Settings::NvidiaDlssQuality mode) {
    using Quality = Settings::NvidiaDlssQuality;
    switch (mode) {
    case Quality::Native:
        return NGX_MODULE_QUALITY_DLAA;
    case Quality::Quality:
        return NGX_MODULE_QUALITY_QUALITY;
    case Quality::Balanced:
        return NGX_MODULE_QUALITY_BALANCED;
    case Quality::Performance:
        return NGX_MODULE_QUALITY_PERFORMANCE;
    case Quality::UltraPerformance:
        return NGX_MODULE_QUALITY_ULTRA_PERFORMANCE;
    case Quality::Automatic:
        break;
    }
    return -1;
}

int NgxPresetFor(Settings::NvidiaDlssPreset preset) {
    using Preset = Settings::NvidiaDlssPreset;
    switch (preset) {
    case Preset::NvidiaDefault:
        return NGX_MODULE_PRESET_DEFAULT;
    case Preset::E:
        return NGX_MODULE_PRESET_E;
    case Preset::F:
        return NGX_MODULE_PRESET_F;
    case Preset::J:
        return NGX_MODULE_PRESET_J;
    case Preset::K:
        return NGX_MODULE_PRESET_K;
    case Preset::L:
        return NGX_MODULE_PRESET_L;
    case Preset::M:
        return NGX_MODULE_PRESET_M;
    }
    return NGX_MODULE_PRESET_M;
}

ngx_module_image ModuleImage(const DlssUpscaler::Image& image, VkExtent2D size) {
    return ngx_module_image{
        .view = reinterpret_cast<u64>(image.view),
        .image = reinterpret_cast<u64>(image.image),
        .format = image.format,
        .aspect = image.range.aspectMask,
        .level_count = image.range.levelCount,
        .layer_count = image.range.layerCount,
        .width = size.width,
        .height = size.height,
    };
}

} // Anonymous namespace

bool DlssUpscaler::Available(const Device& device) {
    return Ngx::EnsureInit(device);
}

DlssUpscaler::DlssUpscaler(const Device& device_, Scheduler& scheduler_,
                           MemoryAllocator& memory_allocator_)
    : device{device_}, scheduler{scheduler_}, memory_allocator{memory_allocator_} {
    if (!Ngx::EnsureInit(device)) {
        throw vk::Exception(VK_ERROR_FEATURE_NOT_PRESENT);
    }
    VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    point_sampler = device.GetLogical().CreateSampler(sampler_info);
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    linear_sampler = device.GetLogical().CreateSampler(sampler_info);
}

DlssUpscaler::~DlssUpscaler() {
    // Not scheduler.Finish(): this runs when the renderer goes away, after the game's channels are
    // released, and a submit would touch their state. Nothing more gets recorded by then.
    void(device.GetLogical().WaitIdle());
    if (ngx_feature != 0) {
        Ngx::ReleaseFeature(ngx_feature);
        ngx_feature = 0;
    }
    Ngx::Shutdown();
}

bool DlssUpscaler::Upscale(const Inputs& in) {
    if (failed) {
        return false;
    }
    if (eval_failures.load(std::memory_order_acquire) >= EVAL_FAILURE_LIMIT) {
        if (feature_recreated) {
            if (!failed.exchange(true)) {
                LOG_ERROR(Render_Vulkan,
                          "NVIDIA DLSS (NGX): still failing after a recreate; using FSR 3.1");
            }
            return false;
        }
        feature_recreated = true;
        scheduler.Finish();
        if (ngx_feature != 0) {
            Ngx::ReleaseFeature(ngx_feature);
            ngx_feature = 0;
        }
        eval_failures.store(0, std::memory_order_release);
        LOG_WARNING(Render_Vulkan, "NVIDIA DLSS (NGX): {} evaluations failed; recreating the feature",
                    EVAL_FAILURE_LIMIT);
    }
    const bool extent_changed = !output_image || in.render_size.width != render_extent.width ||
                                in.render_size.height != render_extent.height ||
                                in.upscale_size.width != upscale_extent.width ||
                                in.upscale_size.height != upscale_extent.height;
    const auto settings = Settings::GetNvidiaDlssRuntimeSettings();
    const bool options_changed =
        extent_changed || in.hdr != hdr_input || in.depth_inverted != depth_inverted_input ||
        quality_mode != settings.quality || model_preset != settings.preset;
    if (create_refused.load() && !options_changed) {
        // The feature create failed; the output stays cleared until the options change.
        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([image = *output_image](vk::CommandBuffer cmdbuf) {
            Dlss::ClearOutput(cmdbuf, image);
        });
        return true;
    }
    const bool reset = in.reset || options_changed;
    if (options_changed && !extent_changed) {
        scheduler.Finish();
    }
    if (extent_changed) {
        CreateOutput(in.upscale_size);
        render_extent = in.render_size;
        upscale_extent = in.upscale_size;
    }
    if (options_changed && ngx_feature != 0) {
        // The GPU is idle here (Finish above or CreateOutput); NGX wants the
        // feature recreated rather than reconfigured.
        Ngx::ReleaseFeature(ngx_feature);
        ngx_feature = 0;
        eval_failures.store(0, std::memory_order_release);
    }
    hdr_input = in.hdr;
    depth_inverted_input = in.depth_inverted;
    quality_mode = settings.quality;
    model_preset = settings.preset;

    // Keep the existing HDR controls for linear input such as Fortune's Weave. Tonemapped
    // input such as Ocarina's already has its exposure applied.
    const bool hdr = in.hdr && Dlss::EnvInt("EDEN_NX2_DLSS_HDR", 1) != 0;
    const bool auto_exposure = in.hdr && Dlss::EnvInt("EDEN_NX2_DLSS_AUTO_EXPOSURE", 1) != 0;
    const int preset = NgxPresetFor(settings.preset);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([this, in, options_changed, requested = settings.quality,
                       preset_setting = settings.preset, reset, hdr, auto_exposure, preset,
                       output_image_handle = *output_image,
                       output_view_handle = *output_view](vk::CommandBuffer cmdbuf) {
        constexpr VkAccessFlags WRITES =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        // The inputs come from the game's draws and dispatches and stay in GENERAL otherwise;
        // DLSS reads them as sampled images.
        const std::array to_read{
            Dlss::Barrier(in.color, WRITES, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            Dlss::Barrier(in.depth, WRITES, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            Dlss::Barrier(in.motion_vectors, WRITES, VK_ACCESS_SHADER_READ_BIT,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        };
        const std::array to_general{
            Dlss::Barrier(in.color, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | WRITES,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            Dlss::Barrier(in.depth, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | WRITES,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            Dlss::Barrier(in.motion_vectors, VK_ACCESS_SHADER_READ_BIT,
                          VK_ACCESS_SHADER_READ_BIT | WRITES, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_GENERAL),
        };
        const VkMemoryBarrier output_written{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        };
        const auto result = VideoCommon::RecordDlssEvaluation(
            [&] {
                if (!options_changed && ngx_feature != 0) {
                    return true;
                }
                namespace Mode = VideoCommon::DlssMode;
                const auto selection = Mode::Select(
                    requested, {in.render_size.width, in.render_size.height},
                    {in.upscale_size.width, in.upscale_size.height},
                    [&](Settings::NvidiaDlssQuality mode) -> std::optional<Mode::Range> {
                        u32 opt_width = 0, opt_height = 0, min_width = 0, min_height = 0,
                           max_width = 0, max_height = 0;
                        float sharpness = 0.0f;
                        if (Ngx::OptimalSettings(in.upscale_size.width, in.upscale_size.height,
                                                 NgxQualityFor(mode), &opt_width, &opt_height,
                                                 &min_width, &min_height, &max_width, &max_height,
                                                 &sharpness) != NGX_MODULE_OK) {
                            return std::nullopt;
                        }
                        return Mode::Range{{min_width, min_height}, {max_width, max_height}};
                    });
                if (selection.fallback) {
                    const Mode::Range range = selection.requested_range.value_or(Mode::Range{});
                    LOG_WARNING(
                        Render_Vulkan,
                        "NVIDIA DLSS requested {} but using {} for {}x{} -> {}x{}; "
                        "requested range {}x{} to {}x{} (query {})",
                        Settings::CanonicalizeEnum(requested),
                        selection.mode ? Settings::CanonicalizeEnum(*selection.mode) : "FSR 3.1",
                        in.render_size.width, in.render_size.height, in.upscale_size.width,
                        in.upscale_size.height, range.min.width, range.min.height, range.max.width,
                        range.max.height, selection.requested_range ? "supported" : "unavailable");
                }
                if (!selection.mode) {
                    LOG_WARNING(Render_Vulkan, "No NVIDIA DLSS mode accepts {}x{} -> {}x{}",
                                in.render_size.width, in.render_size.height,
                                in.upscale_size.width, in.upscale_size.height);
                    return false;
                }
                evaluated_mode = *selection.mode;
                LOG_INFO(Render_Vulkan,
                         "NVIDIA DLSS (NGX): {}x{} -> {}x{}, mode {}, preset {}, {}",
                         in.render_size.width, in.render_size.height, in.upscale_size.width,
                         in.upscale_size.height, Settings::CanonicalizeEnum(*selection.mode),
                         preset, hdr ? "HDR" : "LDR");
                const ngx_module_feature_create create{
                    .render_width = in.render_size.width,
                    .render_height = in.render_size.height,
                    .output_width = in.upscale_size.width,
                    .output_height = in.upscale_size.height,
                    .quality = NgxQualityFor(*selection.mode),
                    .preset = preset,
                    .hdr = hdr,
                    .auto_exposure = auto_exposure,
                    .depth_inverted = in.depth_inverted,
                };
                ngx_feature = Ngx::CreateFeature(*cmdbuf, &create);
                if (ngx_feature == 0) {
                    create_refused.store(true, std::memory_order_relaxed);
                    return false;
                }
                create_refused.store(false, std::memory_order_relaxed);
                return true;
            },
            [&] {
                return true; // NGX needs no frame token
            },
            [&] {
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, {}, {}, to_read);
            },
            [&] {
                return true; // NGX takes its constants with each evaluation
            },
            [&] {
                const ngx_module_eval_params eval{
                    .color = ModuleImage(in.color, in.render_size),
                    .depth = ModuleImage(in.depth, in.depth_size.width != 0
                                                      ? in.depth_size
                                                      : in.render_size),
                    .motion_vectors = ModuleImage(in.motion_vectors, in.render_size),
                    .output =
                        {
                            .view = reinterpret_cast<u64>(output_view_handle),
                            .image = reinterpret_cast<u64>(output_image_handle),
                            .format = Dlss::OUTPUT_FORMAT,
                            .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                            .level_count = 1,
                            .layer_count = 1,
                            .width = in.upscale_size.width,
                            .height = in.upscale_size.height,
                        },
                    .render_width = in.render_size.width,
                    .render_height = in.render_size.height,
                    .jitter_x = in.jitter[0],
                    .jitter_y = in.jitter[1],
                    // NGX takes motion in pixels, and ours already is.
                    .mv_scale_x = 1.0f,
                    .mv_scale_y = 1.0f,
                    .sharpness = 0.0f,
                    .reset = reset,
                };
                if (Ngx::EvaluateFeature(ngx_feature, *cmdbuf, &eval) != NGX_MODULE_OK) {
                    const u32 streak = eval_failures.fetch_add(1, std::memory_order_release) + 1;
                    if (streak == 1) {
                        LOG_ERROR(Render_Vulkan, "NVIDIA DLSS (NGX): evaluation failed: {}",
                                  Ngx::LastError());
                    }
                    return false;
                }
                eval_failures.store(0, std::memory_order_release);
                return true;
            },
            [&] {
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, output_written, {},
                                       to_general);
            });
        if (result == VideoCommon::DlssEvaluationResult::Succeeded) {
            Dlss::last_evaluation.store(static_cast<u32>(evaluated_mode) |
                                      (static_cast<u32>(requested) << 8) |
                                      (static_cast<u32>(preset_setting) << 16),
                                  std::memory_order_relaxed);
            Dlss::evaluations.fetch_add(1, std::memory_order_release);
            // NGX doesn't report the model it picked, but its memory tells them apart: at
            // 3840x2160 output DLSS 310.9 allocates about 198 MB for presets E and F and 307 MB
            // for J and K.
            u64 vram_bytes = 0;
            if (options_changed && Ngx::Stats(&vram_bytes) == NGX_MODULE_OK) {
                LOG_INFO(Render_Vulkan, "NVIDIA DLSS (NGX): preset {} allocated {:.1f} MB", preset,
                         static_cast<double>(vram_bytes) / 1e6);
            }
            return;
        }
        Dlss::ClearOutput(cmdbuf, output_image_handle);
        if (result == VideoCommon::DlssEvaluationResult::OptionsFailed &&
            create_refused.load(std::memory_order_relaxed)) {
            LOG_ERROR(Render_Vulkan, "NVIDIA DLSS (NGX): creating the feature failed: {}",
                      Ngx::LastError());
            return; // retried when the options change
        }
        if (result == VideoCommon::DlssEvaluationResult::EvaluateFailed) {
            return; // logged with the streak; the caller recreates the feature after a run
        }
        if (!failed.exchange(true)) {
            LOG_ERROR(Render_Vulkan,
                      "NVIDIA DLSS (NGX): recording failed at stage {}; using FSR 3.1",
                      static_cast<int>(result));
        }
    });
    // DLSS binds its own pipelines and descriptors on the command buffer.
    scheduler.InvalidateState();
    return true;
}

} // namespace Vulkan
