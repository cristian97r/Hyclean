// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/hle/kernel/nx2_address_wait_trace.h"
#include "core/hle/kernel/nx2_thread_stats.h"

namespace Stats = Kernel::Nx2ThreadStats;

TEST_CASE("Guest thread stats count the gaps of a counter-polling loop", "[nx2][kernel]") {
    constexpr u64 Spin = 52;
    Stats::RunTally run{};
    Stats::CountRead(run, 1000, Spin);
    Stats::CountRead(run, 1005, Spin);
    Stats::CountRead(run, 1040, Spin);
    Stats::CountRead(run, 2000, Spin);
    CHECK(run.reads == 4);
    CHECK(run.poll_reads == 2);
    CHECK(run.poll_ticks == 40);
    CHECK(run.tight_poll_ticks == 5);
}

TEST_CASE("Guest thread stats count hints by type and the time between close ones",
          "[nx2][kernel]") {
    constexpr u64 Spin = 52;
    Stats::RunTally run{};
    Stats::CountHint(run, 4, 500, Spin);
    Stats::CountHint(run, 4, 510, Spin);
    Stats::CountHint(run, 1, 900, Spin);
    CHECK(run.hints[4] == 2);
    CHECK(run.hints[1] == 1);
    CHECK(run.hint_ticks == 10);
}

TEST_CASE("Guest thread stats split read-backs from other cached memory calls", "[nx2][kernel]") {
    Stats::RunTally run{};
    Stats::CountMemory(run, 7, false);
    Stats::CountMemory(run, 300, true);
    CHECK(run.memory_calls == 2);
    CHECK(run.memory_ticks == 307);
    CHECK(run.read_backs == 1);
    CHECK(run.read_back_ticks == 300);
}

TEST_CASE("Guest thread stats name the wait of each blocking SVC", "[nx2][kernel]") {
    static_assert(Stats::WaitOf(0x21) == Stats::Wait::Ipc);
    static_assert(Stats::WaitOf(0x18) == Stats::Wait::SyncObjects);
    static_assert(Stats::WaitOf(0x34) == Stats::Wait::Arbiter);
    static_assert(Stats::WaitOf(0x1C) == Stats::Wait::CondVar);
    static_assert(Stats::WaitOf(0x1A) == Stats::Wait::Lock);
    static_assert(Stats::WaitOf(0x0B) == Stats::Wait::Sleep);
    CHECK(Stats::WaitOf(0x43) == Stats::Wait::Other);
}

TEST_CASE("Address wait trace reports the threads with the most wait time and their wakers",
          "[nx2][kernel]") {
    namespace Trace = Kernel::Nx2AddressWaitTrace;
    Trace::Table table;
    table.Add(88, 0x1000, 3'000'000, 95);
    table.Add(88, 0x1000, 1'000'000, Trace::Timeout);
    table.Add(88, 0x2000, 500'000, 90);
    table.Add(95, 0x1000, 2'000'000, 88);
    const auto lines = table.Report(1.0, [](u64 id) { return "t" + std::to_string(id); });
    REQUIRE(lines.size() == 9);
    CHECK(lines[1] == "  t88 waits 4.50 ms/s on addresses:");
    CHECK(lines[2] == "    0x1000: 2 waits/s, 4.00 ms/s; woken by t95 1, timeout 1");
    CHECK(lines[3] == "    0x2000: 1 waits/s, 0.50 ms/s; woken by t90 1");
    CHECK(lines[4] == "  t95 waits 2.00 ms/s on addresses:");
    CHECK(lines[6] == "  All threads:");
    CHECK(lines[7] == "    0x1000: 3 waits/s, 6.00 ms/s; woken by t88 1, t95 1, timeout 1");
    CHECK(lines[8] == "    0x2000: 1 waits/s, 0.50 ms/s; woken by t90 1");
}
