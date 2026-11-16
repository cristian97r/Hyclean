// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/common_types.h"

namespace Core {
class System;
}

// EDEN_NX2_GUEST_MEMORY_TRACE=1: where the guest RAM backing's resident pages go. Every 10 s, per
// kernel pool heap its bounds, size, free bytes, the RAM its used and free pages hold, and its free
// bytes in granules that may still hold data; per process the bytes mapped and resident by memory
// state (heap, code, stacks, IPC, shared, GPU-only backings as Io...); and the GPU-only backings of
// nx2:gpu. Residency comes from mincore on the backing, which touches no page. The debug server's
// guest_memory method returns the same report.
namespace Kernel::Nx2GuestMemory {

inline const bool g_enabled = std::getenv("EDEN_NX2_GUEST_MEMORY_TRACE") != nullptr;

/// nx2:gpu's GPU-only backings right now and their peak.
inline std::atomic<u64> g_gpu_only_bytes{};
inline std::atomic<u64> g_gpu_only_peak{};

constexpr size_t PageShift = 12;
constexpr size_t GranuleShift = 16;

struct HeapTally {
    u64 size{};
    u64 free{};          ///< Pages nothing holds
    u64 used_resident{}; ///< Held pages in RAM
    u64 free_resident{}; ///< Free pages in RAM: nothing uses them, the host still keeps them
    u64 free_dirty{};    ///< Free pages in granules the next allocation has to clear
};

/// Tallies a heap at `offset` bytes into the backing. `free_pages` and `resident` hold one byte per
/// page (`resident` for the whole backing), `dirty_granules` one per 64 KiB granule.
HeapTally TallyHeap(u64 offset, std::span<const u8> free_pages, std::span<const u8> resident,
                    std::span<const u8> dirty_granules);

struct Heap {
    std::string pool;
    u64 address{}; ///< Physical
    HeapTally tally;
};

struct StateTally {
    u64 mapped{};
    u64 resident{};
};

struct Process {
    u64 id{};
    std::string name;
    std::array<StateTally, 0x18> states{}; ///< By Svc::MemoryState
};

struct Report {
    u64 backing_size{};
    u64 backing_resident{};
    bool residency_known{};
    std::vector<Heap> heaps;
    std::vector<Process> processes;
    u64 gpu_only_bytes{};
    u64 gpu_only_peak{};
};

Report Collect(Core::System& system);
std::vector<std::string> Lines(const Report& report);
nlohmann::json Json(const Report& report);

/// Starts the 10 s report when EDEN_NX2_GUEST_MEMORY_TRACE is set; an empty thread otherwise.
std::jthread MaybeStartTrace(Core::System& system);

} // namespace Kernel::Nx2GuestMemory
