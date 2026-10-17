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
// Return false to cancel before completion. A canceled move retains its source;
// a completed explicit move removes it only after a successful rename or verification.
using ProgressCallback = std::function<bool(const Progress&)>;
Result prepare(const Paths& paths);
std::vector<Disc> scan(const ProgressCallback& progress,
                      const std::vector<std::filesystem::path>& additional = {});
struct BrowseEntry {
    std::filesystem::path path;
    bool directory = false;
};
// Directory listing is read-only and checks file extensions, not every ISO's
// contents. Subdirectories are opened only after the user chooses them.
std::vector<BrowseEntry> browse(const std::filesystem::path& folder,
                              const ProgressCallback& progress = {});
bool requires_move(const std::filesystem::path& source);
// Call only after the user explicitly selects Move and use. External images
// stay in place. A cross-filesystem move removes its source only after a fully
// byte-verified copy; cancellation never removes the source.
std::filesystem::path move_selected(const std::filesystem::path& source,
                                    const ProgressCallback& progress = {});
// Metadata filtering only. Dusklight still performs its normal full verification.
std::vector<Disc> discs(const std::filesystem::path& directory, const ProgressCallback& progress = {});
// Retain an available protected selection; auto-select only an unambiguous disc.
std::string startup_disc(const std::string& saved, const std::filesystem::path& directory);
}
