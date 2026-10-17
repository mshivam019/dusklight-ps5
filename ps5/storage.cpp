// SPDX-License-Identifier: MIT
#include "storage.h"
#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ps5_storage {
namespace fs = std::filesystem;
namespace {
std::optional<Paths> prepared_paths;
void report(const ProgressCallback& callback, Phase phase, const fs::path& file,
            std::uint64_t completed, std::uint64_t total) {
    if (callback && !callback({phase,file.filename().string(),completed,total}))
        throw std::runtime_error("Scan canceled. Original files were retained.");
}
void directory(const fs::path& path) {
    fs::path prefix;
    for (const auto& part : path) {
        prefix /= part;
        if (fs::is_symlink(prefix) || (fs::exists(prefix) && !fs::is_directory(prefix)))
            throw std::runtime_error("Unsafe data directory: " + prefix.string());
    }
    fs::create_directories(path);
}

bool equal_files(const fs::path& a, const fs::path& b, const ProgressCallback& progress = {}) {
    if (fs::is_symlink(a) || fs::is_symlink(b) || !fs::is_regular_file(a) ||
        !fs::is_regular_file(b) || fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left || !right) throw std::runtime_error("Cannot verify data copy");
    std::array<char, 65536> x{}, y{};
    std::uint64_t completed = 0;
    const auto total = fs::file_size(a);
    report(progress,Phase::Verify,a,0,total);
    do {
        left.read(x.data(), x.size());
        right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) return false;
        completed += left.gcount();
        report(progress,Phase::Verify,a,completed,total);
    } while (left && right);
    if (left.bad() || right.bad()) throw std::runtime_error("Data verification read failed");
    return true;
}

void copy_verified(const fs::path& source, const fs::path& target, const ProgressCallback& progress = {}) {
    if (fs::is_symlink(source) || !fs::is_regular_file(source))
        throw std::runtime_error("Source data is not a regular file");
    directory(target.parent_path());
    const fs::path temporary = target.string() + ".importing";
    if (fs::is_symlink(temporary) || (fs::exists(temporary) && !fs::is_regular_file(temporary)))
        throw std::runtime_error("Unsafe interrupted data copy");
    if (fs::exists(target) || fs::is_symlink(target))
        throw std::runtime_error("Refusing to overwrite personal data");
    const auto total = fs::file_size(source);
    std::ifstream input(source,std::ios::binary);
    std::ofstream output(temporary,std::ios::binary | std::ios::trunc);
    if (!input || !output) throw std::runtime_error("Cannot open data copy");
    std::array<char,65536> block{};
    std::uint64_t completed = 0;
    report(progress,Phase::Copy,source,0,total);
    while (input.read(block.data(),block.size()) || input.gcount()) {
        output.write(block.data(),input.gcount());
        if (!output) throw std::runtime_error("Data copy write failed");
        completed += input.gcount();
        report(progress,Phase::Copy,source,completed,total);
    }
    output.close();
    if (input.bad() || !output || completed != total) throw std::runtime_error("Data copy incomplete");
    if (!equal_files(source, temporary, progress)) throw std::runtime_error("Data copy verification failed");
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

// A verified image may remain beside eboot.bin. Avoid rereading multiple GB on
// every launch while both files and their source/destination names are unchanged.
// A size or write-time change invalidates the hint and forces byte comparison.
bool imported_image(const fs::path& source, const fs::path& target, bool verified = false,
                    const ProgressCallback& progress = {}) {
    if (fs::is_symlink(source) || fs::is_symlink(target) ||
        !fs::is_regular_file(source) || !fs::is_regular_file(target)) return false;
    const auto source_name = source.string(), target_name = target.string();
    const auto modified = [](const fs::path& path) {
        // libc++ on PS5 uses a 128-bit file-clock duration; streams accept int64.
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            fs::last_write_time(path).time_since_epoch()).count();
    };
    std::ostringstream identity;
    identity << std::quoted(source_name) << ' ' << std::quoted(target_name) << ' '
             << fs::file_size(source) << ' ' << modified(source) << ' '
             << fs::file_size(target) << ' ' << modified(target);
    const auto stamp = identity.str();
    uint64_t key = 14695981039346656037ULL;
    for (const unsigned char c : source_name + '\n' + target_name) { key ^= c; key *= 1099511628211ULL; }
    std::ostringstream filename;
    filename << std::hex << key;
    const auto cache = target.parent_path() / ".verified-imports" / filename.str();
    directory(cache.parent_path());
    if (fs::is_symlink(cache) || (fs::exists(cache) && !fs::is_regular_file(cache)))
        throw std::runtime_error("Unsafe import receipt");
    if (fs::exists(cache) && fs::file_size(cache) == stamp.size()) {
        std::ifstream file(cache);
        const std::string previous{std::istreambuf_iterator<char>(file), {}};
        if (file && previous == stamp) return true;
    }
    if (!verified && !equal_files(source, target, progress)) return false;
    std::ofstream file(cache, std::ios::trunc);
    file << stamp;
    file.close();
    if (!file) throw std::runtime_error("Cannot record verified image import");
    return true;
}

void import_images(const fs::path& source, const fs::path& target, const ProgressCallback& progress = {}) {
    if (!fs::exists(source)) return;
    if (fs::is_symlink(source)) throw std::runtime_error("Image directory is a symbolic link");
    for (const auto& entry : fs::directory_iterator(source)) {
        report(progress,Phase::Search,entry.path(),0,0);
        if (!image_name(entry.path())) continue;
        if (entry.is_symlink()) throw std::runtime_error("Disc image is a symbolic link");
        if (!entry.is_regular_file()) continue;
        auto destination = target / entry.path().filename();
        unsigned number = 1;
        while (fs::exists(destination) || fs::is_symlink(destination)) {
            if (fs::is_symlink(destination))
                throw std::runtime_error("Persistent image is a symbolic link");
            if (imported_image(entry.path(), destination, false, progress)) break;
            destination = target / (entry.path().stem().string() + "-" +
                std::to_string(number++) + entry.path().extension().string());
        }
        if (!fs::exists(destination)) {
            copy_verified(entry.path(), destination, progress);
            if (!imported_image(entry.path(), destination, true))
                throw std::runtime_error("Imported image changed during verification");
        }
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

Result prepare(const Paths& paths, bool include_images) {
    prepared_paths = paths;
    Result result{paths.root / "user", paths.root / "images", {}};
    try {
        directory(result.user);
        directory(result.images);
        if (include_images) import_images(paths.app, result.images);
        if (include_images) import_images(paths.previous, result.images);
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

std::vector<Disc> discs(const fs::path& path, const ProgressCallback& progress) {
    std::vector<Disc> result;
    if (!fs::exists(path)) return result;
    if (fs::is_symlink(path)) throw std::runtime_error("Disc folder is a symbolic link");
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        report(progress,Phase::Search,entry.path(),0,0);
        entries.push_back(entry);
    }
    std::uint64_t checked = 0;
    for (const auto& entry : entries) {
        report(progress,Phase::Search,entry.path(),++checked,entries.size());
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

std::vector<Disc> scan(const ProgressCallback& progress, const std::vector<fs::path>& additional) {
    if (!prepared_paths) throw std::runtime_error("Protected disc folder is unavailable");
    const auto& paths = *prepared_paths;
    const auto images = paths.root / "images";
    report(progress,Phase::Search,{},0,0);
    for (const auto& source : {paths.download,paths.app,paths.previous}) {
        if (!source.empty()) import_images(source,images,progress);
    }
    auto result = discs(images,progress);
    for (const auto& extra : additional) {
        auto found = discs(extra,progress);
        result.insert(result.end(),found.begin(),found.end());
    }
    return result;
}

std::string startup_disc(const std::string& saved, const fs::path& directory) {
    const auto available = discs(directory);
    for (const auto& disc : available)
        if (disc.path == fs::path(saved)) return saved;
    return available.size() == 1 ? available.front().path.string() : std::string{};
}
}
