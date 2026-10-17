// SPDX-License-Identifier: MIT
// Synthetic data only: exercise region discovery and replacement of the entire app.
#include "storage.h"
#include <cassert>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
void put(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << data;
}
std::string get(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
std::string image(const std::string& id) {
    std::string data(0x1000, '\0');
    data.replace(0, 6, id);
    data.replace(0x1c, 4, "\xc2\x33\x9f\x3d", 4);
    return data;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    fs::path root = argv[1];
    ps5_storage::Paths paths{root / "app", root / "protected", root / "previous", root / "download"};
    put(paths.app / "PAL.ISO", image("GZ2P01"));
    put(paths.app / "unplayable.rvz", "preserve compressed personal data");
    put(paths.app / "user/config.json", "original user settings");
    put(paths.app / "user/MemoryCardA.EUR.raw", "stale packaged save");
    put(paths.download / "user/MemoryCardA.EUR.raw", "live save");
    put(paths.root / "images/PAL.ISO.importing", "partial interrupted copy");
    auto result = ps5_storage::prepare(paths);
    assert(result.error.empty());
    assert(get(result.images / "PAL.ISO") == image("GZ2P01"));
    assert(get(result.images / "unplayable.rvz") == "preserve compressed personal data");
    assert(get(result.user / "MemoryCardA.EUR.raw") == "live save");
    auto discs = ps5_storage::discs(result.images);
    assert(discs.size() == 1 && discs[0].region == "PAL");
    assert(ps5_storage::startup_disc("/app0/game.iso", result.images) == discs[0].path.string());
    put(result.user / "MemoryCardA.EUR.raw", "newer save");
    // Simulate a destructive catalog updater replacing the complete application folder.
    fs::remove_all(paths.app);
    fs::create_directories(paths.app);
    result = ps5_storage::prepare(paths);
    assert(result.error.empty() && get(result.user / "MemoryCardA.EUR.raw") == "newer save");
    assert(get(result.user / "config.json") == "original user settings");
    assert(get(result.images / "PAL.ISO") == image("GZ2P01"));
    put(paths.app / "PAL.ISO", image("GZ2E01"));
    put(paths.previous / "old.gcm", image("GZ2J01"));
    result = ps5_storage::prepare(paths);
    assert(result.error.empty() && get(result.images / "PAL-1.ISO") == image("GZ2E01"));
    assert(get(result.images / "PAL.ISO") == image("GZ2P01"));
    assert(fs::exists(paths.app / "PAL.ISO"));
    discs = ps5_storage::discs(result.images);
    assert(discs.size() == 3);
    assert(ps5_storage::startup_disc("missing.iso", result.images).empty());
    const auto selected = (result.images / "PAL-1.ISO").string();
    assert(ps5_storage::startup_disc(selected, result.images) == selected);
    // Wrong game, unsupported revision, compressed/truncated files never enter the picker.
    put(result.images / "other.iso", image("GZLE01"));
    auto revision = image("GZ2P01"); revision[7] = 1;
    put(result.images / "revision.iso", revision);
    put(result.images / "truncated.iso", "GZ2P01");
    assert(ps5_storage::discs(result.images).size() == 3);
    // Refuse destination symlinks without writing outside the protected root.
    fs::create_directories(root / "outside");
    ps5_storage::Paths unsafe{root / "unsafe-app", root / "unsafe-root", root / "absent", {}};
    fs::create_directories(unsafe.root);
    fs::create_directory_symlink(root / "outside", unsafe.root / "user");
    put(unsafe.app / "user/save.raw", "retained");
    assert(!ps5_storage::prepare(unsafe).error.empty());
    assert(fs::is_empty(root / "outside") && get(unsafe.app / "user/save.raw") == "retained");
    // A directory with a save's filename must not masquerade as a completed import.
    ps5_storage::Paths conflict{root / "conflict-app", root / "conflict-data", root / "absent", {}};
    put(conflict.app / "user/save.raw", "original save");
    fs::create_directories(conflict.root / "user/save.raw");
    assert(!ps5_storage::prepare(conflict).error.empty());
    assert(get(conflict.app / "user/save.raw") == "original save");
    put(root / "param.json", "{\"titleId\":\"PPSA99640\"}");
    assert(ps5_storage::title_id(root / "param.json") == "PPSA99640");
    put(root / "param.json", "{\"titleId\":\"../bad-id\"}");
    bool rejected = false;
    try { ps5_storage::title_id(root / "param.json"); } catch (...) { rejected = true; }
    assert(rejected);
    std::cout << "PAL/USA selection, interrupted copy, update survival, image collisions, save priority and symlinks: PASS\n";
}
