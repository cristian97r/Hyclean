// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/hle/kernel/nx2_guest_memory.h"

namespace GuestMemory = Kernel::Nx2GuestMemory;

TEST_CASE("Guest memory report splits a heap's pages by use, residency and dirt",
          "[nx2][guest_memory]") {
    constexpr u64 Page = 0x1000;
    // The heap starts 16 pages (one granule) into the backing and spans 32 pages.
    std::vector<u8> free_pages(32, 0);
    std::vector<u8> resident(64, 0);
    std::vector<u8> dirty(4, 0);
    for (size_t i = 16; i < 32; ++i) {
        free_pages[i] = 1; // the second granule of the heap is free
    }
    resident[16] = 1;      // heap page 0: used, resident
    resident[16 + 20] = 1; // heap page 20: free, resident
    resident[16 + 21] = 1;
    dirty[2] = 1; // heap pages 16-31

    const auto tally = GuestMemory::TallyHeap(16 * Page, free_pages, resident, dirty);
    CHECK(tally.size == 32 * Page);
    CHECK(tally.free == 16 * Page);
    CHECK(tally.used_resident == Page);
    CHECK(tally.free_resident == 2 * Page);
    CHECK(tally.free_dirty == 16 * Page);
}

TEST_CASE("Guest memory report counts no residency when the host gave none",
          "[nx2][guest_memory]") {
    const std::vector<u8> free_pages{0, 1, 1};
    const auto tally = GuestMemory::TallyHeap(0, free_pages, {}, {});
    CHECK(tally.free == 2 * 0x1000);
    CHECK(tally.used_resident == 0);
    CHECK(tally.free_resident == 0);
    CHECK(tally.free_dirty == 0);
}
