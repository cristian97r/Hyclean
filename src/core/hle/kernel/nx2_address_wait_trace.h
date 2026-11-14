// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "common/common_types.h"

namespace Kernel {
class KThread;
}

// EDEN_NX2_ADDRESS_WAIT_TRACE=1: svcWaitForAddress waits by guest address, waiting thread and
// what ended them (the signalling thread, a timeout or a cancel). Every 10 s the four threads with
// the most address wait time, the presenting threads and those in
// EDEN_NX2_ADDRESS_WAIT_THREADS (thread ids, comma separated) get their top 8 addresses, then the
// top 8 addresses overall. A wait counts in the window it ends in, clipped to that window.
namespace Kernel::Nx2AddressWaitTrace {

inline const bool g_enabled = std::getenv("EDEN_NX2_ADDRESS_WAIT_TRACE") != nullptr;

/// Wakers that are not a guest thread id.
inline constexpr u64 Timeout = ~u64{0};
inline constexpr u64 Host = ~u64{0} - 1;      ///< Signalled from a host (dummy) thread
inline constexpr u64 Cancelled = ~u64{0} - 2; ///< Ended by anything but a signal or timeout

struct Table {
    struct Slot {
        u64 waits{};
        u64 ns{};
        std::map<u64, std::pair<u64, u64>> wakers; ///< Waker -> waits it ended, their ns
    };
    std::map<std::pair<u64, u64>, Slot> slots; ///< (waiting thread id, address)

    void Add(u64 waiter, u64 address, u64 ns, u64 waker);
    /// The report's lines; `name` turns a thread id into its label; `forced` threads are listed
    /// even when they are not among those with the most wait time.
    std::vector<std::string> Report(double seconds, const std::function<std::string(u64)>& name,
                                    const std::set<u64>& forced) const;
};

/// A Signal* call ends `waiter`'s wait; `signaler` is the current thread.
void OnSignal(KThread* waiter, KThread* signaler);
/// `waiter` waited `ns` on `address`; `timed_out` when its timer ended the wait.
void OnWaitEnd(KThread* waiter, u64 address, s64 ns, bool timed_out);
/// `client` presented a frame (PresentClientBuffer).
void OnPresent(KThread* client);

} // namespace Kernel::Nx2AddressWaitTrace
