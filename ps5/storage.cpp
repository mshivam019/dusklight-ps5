// SPDX-License-Identifier: MIT
#include "storage.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ps5_storage {
namespace fs = std::filesystem;
namespace {
void directory(const fs::path& path) {
    fs::path prefix;
    for (const auto& part : path) {
        prefix /= part;
        if (fs::is_symlink(prefix) || (fs::exists(prefix) && !fs::is_directory(prefix)))
            throw std::runtime_error("Unsafe data directory: " + prefix.string());
    }
    fs::create_directories(path);
}

bool equal_files(const fs::path& a, const fs::path& b) {
    if (fs::is_symlink(a) || fs::is_symlink(b) || !fs::is_regular_file(a) ||
        !fs::is_regular_file(b) || fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left || !right) throw std::runtime_error("Cannot verify data copy");
    std::array<char, 65536> x{}, y{};
    do {
        left.read(x.data(), x.size());
        right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) return false;
    } while (left && right);
    if (left.bad() || right.bad()) throw std::runtime_error("Data verification read failed");
    return true;
}

void copy_verified(const fs::path& source, const fs::path& target) {
    if (fs::is_symlink(source) || !fs::is_regular_file(source))
        throw std::runtime_error("Source data is not a regular file");
    directory(target.parent_path());
    const fs::path temporary = target.string() + ".importing";
    if (fs::is_symlink(temporary) || (fs::exists(temporary) && !fs::is_regular_file(temporary)))
        throw std::runtime_error("Unsafe interrupted data copy");
    if (fs::exists(target) || fs::is_symlink(target))
        throw std::runtime_error("Refusing to overwrite personal data");
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
    if (!equal_files(source, temporary)) throw std::runtime_error("Data copy verification failed");
    fs::rename(temporary, target);
}

bool image_name(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    // Preserve compressed images too, without claiming this raw-disc backend plays them.
    return ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".ciso" ||
           ext == ".gcz" || ext == ".wia" || ext == ".wbfs";
}

void import_images(const fs::path& source, const fs::path& target) {
    if (!fs::exists(source)) return;
    if (fs::is_symlink(source)) throw std::runtime_error("Image directory is a symbolic link");
    for (const auto& entry : fs::directory_iterator(source)) {
        if (!image_name(entry.path())) continue;
        if (entry.is_symlink()) throw std::runtime_error("Disc image is a symbolic link");
        if (!entry.is_regular_file()) continue;
        auto destination = target / entry.path().filename();
        unsigned number = 1;
        while (fs::exists(destination) || fs::is_symlink(destination)) {
            if (fs::is_symlink(destination))
                throw std::runtime_error("Persistent image is a symbolic link");
            if (equal_files(entry.path(), destination)) break;
            destination = target / (entry.path().stem().string() + "-" +
                std::to_string(number++) + entry.path().extension().string());
        }
        if (!fs::exists(destination)) copy_verified(entry.path(), destination);
    }
}

void import_user(const fs::path& source, const fs::path& target) {
    if (!fs::exists(source)) return;
    if (fs::is_symlink(source)) throw std::runtime_error("User directory is a symbolic link");
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_symlink()) throw std::runtime_error("User data contains a symbolic link");
        const auto destination = target / entry.path().lexically_relative(source);
        if (entry.is_directory()) directory(destination);
        else if (entry.is_regular_file()) {
            directory(destination.parent_path());
            if (fs::is_symlink(destination)) throw std::runtime_error("Persistent user file is a symbolic link");
            if (fs::exists(destination) && !fs::is_regular_file(destination))
                throw std::runtime_error("Persistent user path is not a regular file");
            if (!fs::exists(destination)) copy_verified(entry.path(), destination);
        }
    }
}
}

std::string title_id(const fs::path& param) {
    std::ifstream file(param);
    std::string text;
    char c;
    while (text.size() < 16384 && file.get(c)) text += c;
    auto pos = text.find("\"titleId\"");
    if (pos != std::string::npos) pos = text.find(':', pos + 9);
    if (pos != std::string::npos) pos = text.find('"', pos + 1);
    if (pos != std::string::npos) {
        auto id = text.substr(pos + 1, 9);
        if (pos + 10 < text.size() && text[pos + 10] == '"' && id.size() == 9 &&
            std::all_of(id.begin(), id.begin() + 4, [](char v) { return v >= 'A' && v <= 'Z'; }) &&
            std::all_of(id.begin() + 4, id.end(), [](char v) { return v >= '0' && v <= '9'; })) return id;
    }
    throw std::runtime_error("Cannot determine this application's title ID");
}

Result prepare(const Paths& paths) {
    Result result{paths.root / "user", paths.root / "images", {}};
    try {
        directory(result.user);
        directory(result.images);
        import_images(paths.app, result.images);
        import_images(paths.previous, result.images);
        // Live writable saves precede packaged defaults. Existing protected files win.
        if (!paths.download.empty()) import_user(paths.download / "user", result.user);
        import_user(paths.app / "user", result.user);
        import_user(paths.previous / "user", result.user);
    } catch (const std::exception& error) {
        result.error = std::string("Could not protect game data: ") + error.what() +
            ". Original files were retained. Check free space and write access to " + paths.root.string();
    }
    return result;
}

std::vector<Disc> discs(const fs::path& path) {
    std::vector<Disc> result;
    if (!fs::exists(path)) return result;
    if (fs::is_symlink(path)) throw std::runtime_error("Disc folder is a symbolic link");
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_symlink() || !entry.is_regular_file() || !image_name(entry.path())) continue;
        std::array<unsigned char, 32> header{};
        std::ifstream stream(entry.path(), std::ios::binary);
        stream.read(reinterpret_cast<char*>(header.data()), header.size());
        if (stream.gcount() != header.size() || header[6] || header[7] ||
            header[0x1c] != 0xc2 || header[0x1d] != 0x33 ||
            header[0x1e] != 0x9f || header[0x1f] != 0x3d) continue;
        const std::string id(reinterpret_cast<char*>(header.data()), 6);
        if (id == "GZ2P01" || id == "GZ2E01" || id == "GZ2J01")
            result.push_back({entry.path(), id == "GZ2P01" ? "PAL" : id == "GZ2E01" ? "USA" : "Japan"});
    }
    std::sort(result.begin(), result.end(), [](const Disc& a, const Disc& b) { return a.path < b.path; });
    return result;
}

std::string startup_disc(const std::string& saved, const fs::path& directory) {
    const auto available = discs(directory);
    for (const auto& disc : available)
        if (disc.path == fs::path(saved)) return saved;
    return available.size() == 1 ? available.front().path.string() : std::string{};
}
}
