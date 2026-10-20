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

// Explicit user requests only: external originals are never removed by copying.
bool is_internal_image(const std::filesystem::path& path);
std::filesystem::path copy_selected(const std::filesystem::path& source, const ProgressCallback& progress = {});
// Capture the configured save root at startup. Failed activation never falls back.
void activate_save_folder(const std::string& configured);
std::filesystem::path save_folder();
bool save_folder_ready() noexcept;
std::filesystem::path card_path(const std::string& game, bool raw, int channel);
// Select an existing directory; optional copy keeps all source saves and refuses conflicts.
std::filesystem::path prepare_save_folder(const std::filesystem::path& target,
    const std::filesystem::path& copy_from = {}, const ProgressCallback& progress = {});
}
