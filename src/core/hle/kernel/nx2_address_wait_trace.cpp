// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <fmt/format.h>
#include "common/logging.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/nx2_address_wait_trace.h"

namespace Kernel::Nx2AddressWaitTrace {

namespace {

constexpr s64 ReportNs = 10'000'000'000;
constexpr size_t ReportThreads = 4;
constexpr size_t ReportAddresses = 8;
constexpr size_t ReportWakers = 3;

std::mutex g_mutex;
std::unordered_map<const KThread*, u64> g_wakers; ///< Waiting thread -> who ended its wait
std::unordered_map<u64, std::string> g_names;     ///< Thread id -> process name
Table g_table;
s64 g_window_start{};

s64 SteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string WakerName(u64 waker, const std::function<std::string(u64)>& name) {
    switch (waker) {
    case Timeout:
        return "timeout";
    case Host:
        return "host";
    case Cancelled:
        return "cancelled";
    default:
        return name(waker);
    }
}

std::string Line(const char* prefix, u64 address, const Table::Slot& slot, double seconds,
                 const std::function<std::string(u64)>& name) {
    std::vector<std::pair<u64, u64>> wakers(slot.wakers.begin(), slot.wakers.end());
    std::stable_sort(wakers.begin(), wakers.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string by;
    for (size_t i = 0; i < std::min(wakers.size(), ReportWakers); ++i) {
        by += fmt::format("{}{} {}", i == 0 ? "" : ", ", WakerName(wakers[i].first, name),
                          wakers[i].second);
    }
    return fmt::format("{}{:#x}: {:.0f} waits/s, {:.2f} ms/s; woken by {}", prefix, address,
                       static_cast<double>(slot.waits) / seconds,
                       static_cast<double>(slot.ns) / 1e6 / seconds, by);
}

} // Anonymous namespace

void Table::Add(u64 waiter, u64 address, u64 ns, u64 waker) {
    Slot& slot = slots[{waiter, address}];
    ++slot.waits;
    slot.ns += ns;
    ++slot.wakers[waker];
}

std::vector<std::string> Table::Report(double seconds,
                                       const std::function<std::string(u64)>& name) const {
    std::map<u64, u64> thread_ns;
    std::map<u64, Slot> by_address;
    for (const auto& [key, slot] : slots) {
        thread_ns[key.first] += slot.ns;
        Slot& total = by_address[key.second];
        total.waits += slot.waits;
        total.ns += slot.ns;
        for (const auto& [waker, count] : slot.wakers) {
            total.wakers[waker] += count;
        }
    }
    const auto by_ns = [](const auto& a, const auto& b) { return a.second > b.second; };
    std::vector<std::pair<u64, u64>> threads(thread_ns.begin(), thread_ns.end());
    std::sort(threads.begin(), threads.end(), by_ns);

    std::vector<std::string> lines;
    lines.push_back(fmt::format("Address waits over {:.1f} s:", seconds));
    for (size_t t = 0; t < std::min(threads.size(), ReportThreads); ++t) {
        const u64 thread = threads[t].first;
        lines.push_back(fmt::format("  {} waits {:.2f} ms/s on addresses:", name(thread),
                                    static_cast<double>(threads[t].second) / 1e6 / seconds));
        std::vector<std::pair<u64, u64>> addresses;
        for (auto it = slots.lower_bound({thread, 0}); it != slots.end() && it->first.first == thread;
             ++it) {
            addresses.emplace_back(it->first.second, it->second.ns);
        }
        std::sort(addresses.begin(), addresses.end(), by_ns);
        for (size_t a = 0; a < std::min(addresses.size(), ReportAddresses); ++a) {
            lines.push_back(Line("    ", addresses[a].first,
                                 slots.at({thread, addresses[a].first}), seconds, name));
        }
    }
    std::vector<std::pair<u64, u64>> addresses;
    for (const auto& [address, slot] : by_address) {
        addresses.emplace_back(address, slot.ns);
    }
    std::sort(addresses.begin(), addresses.end(), by_ns);
    lines.push_back("  All threads:");
    for (size_t a = 0; a < std::min(addresses.size(), ReportAddresses); ++a) {
        lines.push_back(
            Line("    ", addresses[a].first, by_address.at(addresses[a].first), seconds, name));
    }
    return lines;
}

void OnSignal(KThread* waiter, KThread* signaler) {
    const bool guest = signaler != nullptr && !signaler->IsDummyThread() &&
                       signaler->GetOwnerProcess() != nullptr;
    std::scoped_lock lk{g_mutex};
    g_wakers[waiter] = guest ? signaler->GetThreadId() : Host;
    if (guest) {
        g_names.try_emplace(signaler->GetThreadId(), signaler->GetOwnerProcess()->GetName());
    }
}

void OnWaitEnd(KThread* waiter, u64 address, s64 ns, bool timed_out) {
    const s64 now = SteadyNs();
    std::scoped_lock lk{g_mutex};
    u64 waker = timed_out ? Timeout : Cancelled;
    if (const auto it = g_wakers.find(waiter); it != g_wakers.end()) {
        waker = timed_out ? Timeout : it->second;
        g_wakers.erase(it);
    }
    const u64 id = waiter->GetThreadId();
    if (KProcess* process = waiter->GetOwnerProcess()) {
        g_names.try_emplace(id, process->GetName());
    }
    g_table.Add(id, address, static_cast<u64>(std::max<s64>(ns, 0)), waker);
    if (g_window_start == 0) {
        g_window_start = now;
    } else if (now - g_window_start >= ReportNs) {
        const auto name = [](u64 thread) {
            const auto it = g_names.find(thread);
            return fmt::format("t{} {}", thread, it != g_names.end() ? it->second : "?");
        };
        for (const std::string& line :
             g_table.Report(static_cast<double>(now - g_window_start) / 1e9, name)) {
            LOG_INFO(Kernel, "{}", line);
        }
        g_table = {};
        g_window_start = now;
    }
}

} // namespace Kernel::Nx2AddressWaitTrace
