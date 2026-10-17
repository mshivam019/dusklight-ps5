// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <functional>
#include <cstdint>
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
enum class Phase { Search, Copy, Verify };
struct Progress {
    Phase phase = Phase::Search;
    std::string file;
    std::uint64_t completed = 0, total = 0;
};
// Return false to cancel. Original files are always retained; only fully
// verified copies are renamed from .importing into the protected image folder.
using ProgressCallback = std::function<bool(const Progress&)>;
Result prepare(const Paths& paths, bool include_images = true);
std::vector<Disc> scan(const ProgressCallback& progress,
                      const std::vector<std::filesystem::path>& additional = {});
// Metadata filtering only. Dusklight still performs its normal full verification.
std::vector<Disc> discs(const std::filesystem::path& directory, const ProgressCallback& progress = {});
// Retain an available protected selection; auto-select only an unambiguous disc.
std::string startup_disc(const std::string& saved, const std::filesystem::path& directory);
}
