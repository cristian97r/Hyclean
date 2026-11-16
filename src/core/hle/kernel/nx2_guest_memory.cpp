// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include "common/logging.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/nx2_guest_memory.h"

namespace Kernel::Nx2GuestMemory {

namespace {

constexpr std::array<const char*, 0x18> StateNames{
    "free",
    "io",
    "static",
    "code",
    "code_data",
    "normal",
    "shared",
    "alias",
    "alias_code",
    "alias_code_data",
    "ipc",
    "stack",
    "thread_local",
    "transferred",
    "shared_transferred",
    "shared_code",
    "inaccessible",
    "non_secure_ipc",
    "non_device_ipc",
    "kernel",
    "generated_code",
    "code_out",
    "coverage",
    "insecure",
};

const char* PoolName(KMemoryManager::Pool pool) {
    switch (pool) {
    case KMemoryManager::Pool::Application:
        return "application";
    case KMemoryManager::Pool::Applet:
        return "applet";
    case KMemoryManager::Pool::System:
        return "system";
    case KMemoryManager::Pool::SystemNonSecure:
        return "system_non_secure";
    default:
        return "unknown";
    }
}

u64 MiB(u64 bytes) {
    return bytes >> 20;
}

Process CollectProcess(KProcess& process, const u8* backing, u64 backing_size,
                       std::span<const u8> resident) {
    Process out{process.GetProcessId(), process.GetName()};
    const auto& page_table = process.GetPageTable();
    const auto& entries = page_table.GetImpl().entries;
    u64 address = GetInteger(page_table.GetAddressSpaceStart());
    while (true) {
        KMemoryInfo info{};
        Svc::PageInfo page_info{};
        if (page_table.QueryInfo(&info, &page_info, address).IsError()) {
            break;
        }
        const Svc::MemoryInfo svc = info.GetSvcMemoryInfo();
        const auto state = static_cast<size_t>(svc.state);
        if (svc.state != Svc::MemoryState::Free && svc.state != Svc::MemoryState::Inaccessible &&
            state < out.states.size()) {
            StateTally& tally = out.states[state];
            tally.mapped += svc.size;
            const u64 end = svc.base_address + svc.size;
            for (u64 page = svc.base_address >> PageShift;
                 !resident.empty() && page < (end >> PageShift) && page < entries.size(); ++page) {
                const uintptr_t base = entries[page].Pointer(true);
                if (base == 0) {
                    continue;
                }
                const auto host = reinterpret_cast<const u8*>(base + (page << PageShift));
                if (host < backing || host >= backing + backing_size) {
                    continue;
                }
                if (resident[static_cast<size_t>(host - backing) >> PageShift] != 0) {
                    tally.resident += u64{1} << PageShift;
                }
            }
        }
        const u64 next = svc.base_address + svc.size;
        if (next <= address) {
            break; // the fake block past the address space ends at 2^64
        }
        address = next;
    }
    return out;
}

} // namespace

HeapTally TallyHeap(u64 offset, std::span<const u8> free_pages, std::span<const u8> resident,
                    std::span<const u8> dirty_granules) {
    HeapTally tally{.size = u64{free_pages.size()} << PageShift};
    constexpr u64 Page = u64{1} << PageShift;
    for (size_t i = 0; i < free_pages.size(); ++i) {
        const u64 at = offset + (u64{i} << PageShift);
        const size_t page = static_cast<size_t>(at >> PageShift);
        const bool in_ram = page < resident.size() && resident[page] != 0;
        if (free_pages[i] == 0) {
            tally.used_resident += in_ram ? Page : 0;
            continue;
        }
        tally.free += Page;
        tally.free_resident += in_ram ? Page : 0;
        const size_t granule = static_cast<size_t>(at >> GranuleShift);
        if (granule < dirty_granules.size() && dirty_granules[granule] != 0) {
            tally.free_dirty += Page;
        }
    }
    return tally;
}

Report Collect(Core::System& system) {
    Report report;
    const auto& buffer = system.DeviceMemory().buffer;
    const std::vector<u8> resident = buffer.BackingResidency();
    const std::vector<u8> dirty = buffer.DirtyGranules();
    report.backing_size = buffer.BackingSize();
    report.residency_known = !resident.empty();
    report.backing_resident = static_cast<u64>(std::count_if(resident.begin(), resident.end(),
                                                             [](u8 page) { return page != 0; }))
                              << PageShift;

    auto& kernel = system.Kernel();
    for (const auto& heap : kernel.MemoryManager().GetHeapInfo()) {
        const u64 offset = GetInteger(heap.address) - Core::DramMemoryMap::Base;
        report.heaps.push_back({PoolName(heap.pool), GetInteger(heap.address),
                                TallyHeap(offset, heap.free_pages, resident, dirty)});
    }
    for (auto& process : kernel.GetProcessList()) {
        report.processes.push_back(
            CollectProcess(*process, buffer.BackingBasePointer(), report.backing_size, resident));
    }
    report.gpu_only_bytes = g_gpu_only_bytes.load(std::memory_order_relaxed);
    report.gpu_only_peak = g_gpu_only_peak.load(std::memory_order_relaxed);
    return report;
}

std::vector<std::string> Lines(const Report& report) {
    std::vector<std::string> lines;
    lines.push_back(fmt::format(
        "Guest memory: backing {} MiB, {} MiB resident{}; GPU-only backings {} MiB (peak {} MiB)",
        MiB(report.backing_size), MiB(report.backing_resident),
        report.residency_known ? "" : " (unknown on this host)", MiB(report.gpu_only_bytes),
        MiB(report.gpu_only_peak)));
    for (const Heap& heap : report.heaps) {
        const HeapTally& t = heap.tally;
        lines.push_back(fmt::format(
            "Guest memory pool {} [{:#x}, {:#x}): {} MiB, used {} MiB ({} resident), free {} MiB "
            "({} resident, {} dirty)",
            heap.pool, heap.address, heap.address + t.size, MiB(t.size), MiB(t.size - t.free),
            MiB(t.used_resident), MiB(t.free), MiB(t.free_resident), MiB(t.free_dirty)));
    }
    for (const Process& process : report.processes) {
        std::string states;
        for (size_t i = 0; i < process.states.size(); ++i) {
            const StateTally& t = process.states[i];
            if (t.mapped >= (u64{1} << 20)) {
                states += fmt::format(" {}={}/{}", StateNames[i], MiB(t.mapped), MiB(t.resident));
            }
        }
        lines.push_back(fmt::format("Guest memory process {} {} (mapped/resident MiB):{}",
                                    process.id, process.name, states));
    }
    return lines;
}

nlohmann::json Json(const Report& report) {
    nlohmann::json heaps = nlohmann::json::array();
    for (const Heap& heap : report.heaps) {
        const HeapTally& t = heap.tally;
        heaps.push_back({{"pool", heap.pool},
                         {"address", fmt::format("{:#x}", heap.address)},
                         {"end", fmt::format("{:#x}", heap.address + t.size)},
                         {"size", t.size},
                         {"used", t.size - t.free},
                         {"used_resident", t.used_resident},
                         {"free", t.free},
                         {"free_resident", t.free_resident},
                         {"free_dirty", t.free_dirty}});
    }
    nlohmann::json processes = nlohmann::json::array();
    for (const Process& process : report.processes) {
        nlohmann::json states = nlohmann::json::object();
        for (size_t i = 0; i < process.states.size(); ++i) {
            const StateTally& t = process.states[i];
            if (t.mapped != 0) {
                states[StateNames[i]] = {{"mapped", t.mapped}, {"resident", t.resident}};
            }
        }
        processes.push_back({{"id", process.id}, {"name", process.name}, {"states", states}});
    }
    return {{"backing_size", report.backing_size},
            {"backing_resident", report.backing_resident},
            {"residency_known", report.residency_known},
            {"gpu_only_bytes", report.gpu_only_bytes},
            {"gpu_only_peak", report.gpu_only_peak},
            {"heaps", heaps},
            {"processes", processes},
            {"lines", Lines(report)}};
}

std::jthread MaybeStartTrace(Core::System& system) {
    if (!g_enabled) {
        return {};
    }
    return std::jthread([&system](std::stop_token stop) {
        Common::SetCurrentThreadName("GuestMemoryTrace");
        while (Common::StoppableTimedWait(stop, std::chrono::seconds{10})) {
            for (const std::string& line : Lines(Collect(system))) {
                LOG_INFO(Kernel, "{}", line);
            }
        }
    });
}

} // namespace Kernel::Nx2GuestMemory
