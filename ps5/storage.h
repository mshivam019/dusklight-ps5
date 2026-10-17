// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace ps5_storage {
struct Disc {
    std::filesystem::path path;
    std::string region;
};
struct Paths {
    std::filesystem::path app, root, previous, download;
};
struct Result {
    std::filesystem::path user, images;
    std::string error;
};
std::string title_id(const std::filesystem::path& param);
Result prepare(const Paths& paths);
// Metadata filtering only. Dusklight still performs its normal full verification.
std::vector<Disc> discs(const std::filesystem::path& directory);
// Retain an available protected selection; auto-select only an unambiguous disc.
std::string startup_disc(const std::string& saved, const std::filesystem::path& directory);
}
