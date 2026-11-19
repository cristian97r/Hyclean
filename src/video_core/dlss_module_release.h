// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 tasarren

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace VideoCommon {

// The Lichii DLSS module's asset addresses in a GitHub release.
struct DlssModuleRelease {
    std::string tag;
    std::string module_url;
    std::string sha256_url;
};

// Picks lichii-dlss-module.so and its .sha256 from a releases/latest reply.
// Nothing when the reply is not a release or one of the two assets is missing.
inline std::optional<DlssModuleRelease> PickDlssModuleRelease(std::string_view json_text) {
    const nlohmann::json json = nlohmann::json::parse(json_text, nullptr, false);
    if (!json.is_object() || !json.contains("assets") || !json["assets"].is_array()) {
        return std::nullopt;
    }
    DlssModuleRelease release{.tag = json.value("tag_name", std::string{})};
    for (const auto& asset : json["assets"]) {
        if (!asset.is_object()) {
            continue;
        }
        const std::string name = asset.value("name", std::string{});
        const std::string url = asset.value("browser_download_url", std::string{});
        if (name == "lichii-dlss-module.so") {
            release.module_url = url;
        } else if (name == "lichii-dlss-module.so.sha256") {
            release.sha256_url = url;
        }
    }
    if (release.module_url.empty() || release.sha256_url.empty()) {
        return std::nullopt;
    }
    return release;
}

// The lowercase hex sum at the start of a sha256sum line ("<hex>  <name>").
inline std::optional<std::string> ParseSha256Line(std::string_view text) {
    if (text.size() < 64) {
        return std::nullopt;
    }
    std::string hex{text.substr(0, 64)};
    if (!std::ranges::all_of(hex, [](unsigned char c) { return std::isxdigit(c); }) ||
        (text.size() > 64 && !std::isspace(static_cast<unsigned char>(text[64])))) {
        return std::nullopt;
    }
    std::ranges::transform(hex, hex.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return hex;
}

} // namespace VideoCommon
