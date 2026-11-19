// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "video_core/dlss_module_release.h"

using VideoCommon::ParseSha256Line;
using VideoCommon::PickDlssModuleRelease;

TEST_CASE("The DLSS module download picks the module and its sum from the release",
          "[dlss_module_release]") {
    constexpr std::string_view base =
        "https://github.com/tasarren/lichii-dlss/releases/download/v1.0.0/";
    const nlohmann::json reply{
        {"tag_name", "v1.0.0"},
        {"assets",
         {
             {{"name", "LICENSE"}, {"browser_download_url", std::string{base} + "LICENSE"}},
             {{"name", "lichii-dlss-module.so.sha256"},
              {"browser_download_url", std::string{base} + "lichii-dlss-module.so.sha256"}},
             {{"name", "lichii-dlss-module.so"},
              {"browser_download_url", std::string{base} + "lichii-dlss-module.so"}},
         }},
    };
    const auto release = PickDlssModuleRelease(reply.dump());
    REQUIRE(release);
    CHECK(release->tag == "v1.0.0");
    CHECK(release->module_url == std::string{base} + "lichii-dlss-module.so");
    CHECK(release->sha256_url == std::string{base} + "lichii-dlss-module.so.sha256");

    nlohmann::json no_sum = reply;
    no_sum["assets"].erase(1);
    CHECK_FALSE(PickDlssModuleRelease(no_sum.dump()));
    CHECK_FALSE(PickDlssModuleRelease(R"({"message":"Not Found"})"));
    CHECK_FALSE(PickDlssModuleRelease("not json"));
    CHECK_FALSE(PickDlssModuleRelease("[]"));
}

TEST_CASE("The DLSS module sum is read from a sha256sum line", "[dlss_module_release]") {
    const std::string hex(64, 'a');
    CHECK(ParseSha256Line(hex + "  lichii-dlss-module.so\n") == hex);
    CHECK(ParseSha256Line(hex) == hex);
    CHECK(ParseSha256Line(std::string(64, 'A') + "\n") == hex);
    CHECK_FALSE(ParseSha256Line(std::string(63, 'a')));
    CHECK_FALSE(ParseSha256Line(std::string(63, 'a') + "g  x"));
    CHECK_FALSE(ParseSha256Line(std::string(65, 'a')));
}
