// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_descriptor_pool.h"

// Pipelines with the same bindings share one set and pipeline layout; a failed make leaves no
// entry, so the next pipeline tries again.
TEST_CASE("Pipeline layout cache: one make per key, none kept after a throw",
          "[pipeline_layout_cache]") {
    Vulkan::PipelineLayoutCache cache;
    int makes = 0;
    const auto make = [&] {
        ++makes;
        return Vulkan::PipelineLayoutCache::Entry{};
    };
    const auto& first = cache.Get({1, 0, 6, 1, 16}, make);
    CHECK(&cache.Get({1, 0, 6, 1, 16}, make) == &first);
    CHECK(makes == 1);
    CHECK(&cache.Get({1, 0, 6, 2, 16}, make) != &first);
    CHECK(makes == 2);

    CHECK_THROWS_AS(cache.Get({3}, [&]() -> Vulkan::PipelineLayoutCache::Entry {
        throw std::runtime_error("no memory");
    }),
                    std::runtime_error);
    cache.Get({3}, make);
    CHECK(makes == 3);
}
