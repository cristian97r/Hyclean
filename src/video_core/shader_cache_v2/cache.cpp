// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/assert.h"
#include "common/logging.h"
#include "video_core/native_lowering.h"
#include "video_core/nx2_stutter_trace.h"
#include "video_core/shader_cache_v2/cache.h"
#include "video_core/shader_cache_v2/cache_file.h"
#include "video_core/shader_cache_v2/cache_format.h"
#include "video_core/shader_cache_v2/records.h"

namespace VideoCommon::ShaderCacheV2 {
Cache::Cache(std::filesystem::path file, WriterOptions options)
    : writer{std::move(file), options} {}

Cache::~Cache() {
    const auto load = [this](ProgramLookup lookup, std::atomic<u64> ProgramCounts::*field) {
        return (counts[static_cast<size_t>(lookup)].*field).load(std::memory_order_relaxed);
    };
    const auto us_each = [](u64 ns, u64 lookups) {
        return lookups == 0 ? 0.0 : static_cast<double>(ns) / 1000.0 / static_cast<double>(lookups);
    };
    const u64 hits = load(ProgramLookup::Hit, &ProgramCounts::lookups);
    const u64 misses = load(ProgramLookup::Miss, &ProgramCounts::lookups);
    const u64 stale = load(ProgramLookup::Stale, &ProgramCounts::lookups);
    const u64 lowered = misses + stale;
    if (hits + lowered == 0) {
        return;
    }
    const u64 lowered_lookup_ns = load(ProgramLookup::Miss, &ProgramCounts::lookup_ns) +
                                  load(ProgramLookup::Stale, &ProgramCounts::lookup_ns);
    const u64 lowering_ns = load(ProgramLookup::Miss, &ProgramCounts::lowering_ns) +
                            load(ProgramLookup::Stale, &ProgramCounts::lowering_ns);
    LOG_INFO(Shader,
             "Shader cache v2 programs: {} from records ({:.1f} us each to capture and look up), "
             "{} lowered ({} without a record, {} with a stale one; {:.1f} us each to capture and "
             "look up, then {:.1f} us to lower); rereads {} read, {} failed",
             hits, us_each(load(ProgramLookup::Hit, &ProgramCounts::lookup_ns), hits), lowered,
             misses, stale, us_each(lowered_lookup_ns, lowered), us_each(lowering_ns, lowered),
             rereads_ok.load(std::memory_order_relaxed),
             rereads_failed.load(std::memory_order_relaxed));
}

void Cache::AddProgram(u64 key, u64 lowering_stamp) {
    std::scoped_lock lock{mutex};
    programs.insert_or_assign(key, lowering_stamp);
}

void Cache::LoadPrograms(std::span<const LogRecord> live) {
    ASSERT(!loaded_ready.load(std::memory_order_relaxed));
    std::unordered_map<u64, LoadedProgram> table;
    for (const LogRecord& entry : live) {
        if (entry.kind != RecordKind::Program) {
            continue;
        }
        const std::optional<u64> stamp = ProgramStamp(entry.payload);
        if (!stamp) {
            continue;
        }
        AddProgram(entry.key, *stamp);
        if (*stamp != LoweringStamp()) {
            continue;
        }
        std::optional<ProgramRecord> record = DecodeProgram(entry.payload);
        // A record under another key than its inputs make would never be found
        if (!record || ProgramKey(*record) != entry.key) {
            continue;
        }
        const u128 check = ProgramCheck(ProgramKeyInputsOf(*record));
        LoadedProgram& program = table[entry.key];
        program.check = check;
        program.offset.store(entry.offset, std::memory_order_relaxed);
        program.payload_size = entry.payload.size();
    }
    loaded = std::move(table);
    loaded_ready.store(true, std::memory_order_release);
}

FoundProgram Cache::FindProgram(const ProgramKeyInputs& inputs) {
    const u64 key = ProgramKey(inputs);
    if (loaded_ready.load(std::memory_order_acquire)) {
        if (const auto it = loaded.find(key);
            it != loaded.end() && it->second.check == ProgramCheck(inputs)) {
            // A lookup while a compaction renames the file may read the old offset there and
            // lower again; the offset follows right after.
            const std::optional<std::vector<u8>> payload = ReadRecordPayload(
                writer.File(), RecordKind::Program, key,
                it->second.offset.load(std::memory_order_relaxed), it->second.payload_size);
            std::optional<ProgramRecord> record =
                payload ? DecodeProgram(*payload) : std::nullopt;
            if (record) {
                // The game is usually killed, not closed: count as it goes.
                if (const u64 ok = rereads_ok.fetch_add(1, std::memory_order_relaxed) + 1;
                    ok % 1024 == 0) {
                    LOG_INFO(Shader, "Shader cache v2 program rereads: {} read, {} failed", ok,
                             rereads_failed.load(std::memory_order_relaxed));
                }
                return {ProgramLookup::Hit, TakeProgramCode(*record)};
            }
            const u64 failed = rereads_failed.fetch_add(1, std::memory_order_relaxed) + 1;
            LOG_WARNING(Shader,
                        "Shader cache v2: program {:016X} is no longer where the boot load read "
                        "it; lowered again ({} rereads failed, {} read)",
                        key, failed, rereads_ok.load(std::memory_order_relaxed));
        }
    }
    std::scoped_lock lock{mutex};
    const auto it = programs.find(key);
    return {it != programs.end() && it->second != LoweringStamp() ? ProgramLookup::Stale
                                                                  : ProgramLookup::Miss,
            nullptr};
}

void Cache::Compact() {
    writer.Compact({.moved = [this](std::span<const std::pair<const LogRecord*, u64>> moved) {
        if (!loaded_ready.load(std::memory_order_acquire)) {
            return;
        }
        for (const auto& [record, offset] : moved) {
            const auto it = loaded.find(record->key);
            if (record->kind == RecordKind::Program && it != loaded.end() &&
                it->second.offset.load(std::memory_order_relaxed) == record->offset) {
                it->second.offset.store(offset, std::memory_order_relaxed);
            }
        }
    }});
}

void Cache::CountProgram(ProgramLookup lookup, u64 lookup_ns, u64 lowering_ns) {
    ProgramCounts& count = counts[static_cast<size_t>(lookup)];
    count.lookups.fetch_add(1, std::memory_order_relaxed);
    count.lookup_ns.fetch_add(lookup_ns, std::memory_order_relaxed);
    count.lowering_ns.fetch_add(lowering_ns, std::memory_order_relaxed);
    namespace StutterTrace = VideoCommon::StutterTrace;
    StutterTrace::CountLookup(lookup == ProgramLookup::Hit    ? StutterTrace::Lookup::ProgramHit
                              : lookup == ProgramLookup::Miss ? StutterTrace::Lookup::ProgramMiss
                                                              : StutterTrace::Lookup::ProgramStale);
}

void Cache::RecordProgram(const ProgramRecord& record) {
    const u64 key = ProgramKey(record);
    {
        std::scoped_lock lock{mutex};
        const auto [it, inserted] = programs.try_emplace(key, record.lowering_stamp);
        if (!inserted) {
            if (it->second == record.lowering_stamp || keep_stale) {
                return;
            }
            it->second = record.lowering_stamp;
        }
    }
    writer.Append(RecordKind::Program, key, EncodeProgram(record));
}

void Cache::KeepStalePrograms() {
    std::scoped_lock lock{mutex};
    keep_stale = true;
}

} // namespace VideoCommon::ShaderCacheV2
