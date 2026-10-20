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
    // A failed/canceled copy must not strand a large temporary ISO on the SSD.
    // Declared before streams so they close before cleanup runs.
    struct PartialCopy {
        fs::path path;
        ~PartialCopy() { std::error_code ec; fs::remove(path,ec); }
    } cleanup{temporary};
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

void import_user(const fs::path& source, const fs::path& target) {
    if (!fs::exists(source)) return;
    if (fs::is_symlink(source)) throw std::runtime_error("User directory is a symbolic link");
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_symlink()) throw std::runtime_error("User data contains a symbolic link");
        const auto destination = target / entry.path().lexically_relative(source);
        if (entry.is_directory()) directory(destination);
        // A disc placed below user/ or portable data/ is still an ISO,
        // not a setting/save. Never duplicate it during user migration.
        else if (entry.is_regular_file() && !image_name(entry.path())) {
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
    prepared_paths = paths;
    Result result{paths.root / "user", paths.root / "images", {}};
    try {
        directory(result.user);
        directory(result.images);
        const auto marker=paths.root/".userdata-migration-v1.complete";
        if (fs::is_symlink(marker)) throw std::runtime_error("Unsafe migration marker");
        if (fs::exists(marker)) {
            std::ifstream completed(marker);std::string version;std::getline(completed,version);
            if (version!="Dusklight userdata migration v1") throw std::runtime_error("Invalid migration marker");
        } else {
            // Live writable saves precede packaged defaults. Existing protected files win.
            if (!paths.download.empty()) import_user(paths.download / "user", result.user);
            import_user(paths.app / "user", result.user);
            import_user(paths.previous / "user", result.user);
            const auto pending=fs::path(marker.string()+".importing");
            if (fs::is_symlink(pending) || (fs::exists(pending) && !fs::is_regular_file(pending)))
                throw std::runtime_error("Unsafe migration marker");
            std::ofstream completed(pending);completed<<"Dusklight userdata migration v1\n";completed.close();
            if (!completed) throw std::runtime_error("Cannot finish userdata migration");
            fs::rename(pending,marker);
        }
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
    // Discovery never imports images. In particular, deleting a protected ISO
    // must not cause a surviving app/backup copy to be silently restored.
    auto result = discs(images,progress);
    for (const auto& extra : additional) {
        auto found = discs(extra,progress);
        result.insert(result.end(),found.begin(),found.end());
    }
    return result;
}

std::vector<BrowseEntry> browse(const fs::path& requested, const ProgressCallback& progress) {
    const auto folder = requested.lexically_normal();
    if (!folder.is_absolute() || fs::is_symlink(folder))
        throw std::runtime_error("Cannot open this folder.");
    std::vector<BrowseEntry> result;
    std::uint64_t checked = 0;
    for (const auto& entry : fs::directory_iterator(folder)) {
        report(progress,Phase::Search,entry.path(),++checked,0);
        std::error_code ec;
        if (entry.is_symlink(ec) || ec) continue;
        if (entry.is_directory(ec) && !ec) {
            result.push_back({entry.path(),true});
            continue;
        }
        if (!entry.is_regular_file(ec) || ec) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        if (extension == ".iso" || extension == ".gcm")
            result.push_back({entry.path(),false});
    }
    std::sort(result.begin(),result.end(),[](const BrowseEntry& a,const BrowseEntry& b) {
        if (a.directory != b.directory) return a.directory;
        return a.path.filename() < b.path.filename();
    });
    report(progress,Phase::Search,folder,checked,checked);
    return result;
}

namespace {
bool inside(const fs::path& path,const fs::path& root) {
    if (root.empty()) return false;
    auto value = path.lexically_normal(), base = root.lexically_normal();
    while (base != base.root_path() && base.filename().empty()) base = base.parent_path();
    auto a = value.begin(), b = base.begin();
    for (; b != base.end(); ++a,++b) {
        if (a == value.end() || *a != *b) return false;
    }
    return true;
}
}

bool requires_move(const fs::path& source) {
    if (!prepared_paths) throw std::runtime_error("Protected disc folder is unavailable");
    const auto& paths = *prepared_paths;
    return inside(source,paths.app) || inside(source,paths.download) ||
           inside(source,paths.previous) ||
           inside(source,fs::path("/data/homebrew") / paths.root.filename());
}

fs::path move_selected(const fs::path& requested, const ProgressCallback& progress) {
    const auto source = requested.lexically_normal();
    if (!source.is_absolute() || fs::is_symlink(source) || !fs::is_regular_file(source))
        throw std::runtime_error("Source data is not a regular file");
    // Images on USB or elsewhere outside this app already survive its updates.
    if (!requires_move(source)) return source;
    const auto candidates = discs(source.parent_path(),progress);
    if (std::none_of(candidates.begin(),candidates.end(),[&](const Disc& disc) { return disc.path == source; }))
        throw std::runtime_error("The selected game is not supported by Dusklight.");
    const auto images = prepared_paths->root / "images";
    directory(images);
    auto target = images / source.filename();
    const auto original_size = fs::file_size(source);
    const auto original_time = fs::last_write_time(source);
    unsigned number = 1;
    bool already_equal = false;
    while (fs::exists(target) || fs::is_symlink(target)) {
        if (fs::is_symlink(target)) throw std::runtime_error("Persistent image is a symbolic link");
        if (fs::is_regular_file(target) && equal_files(source,target,progress)) {
            already_equal = true;
            break;
        }
        target = images / (source.stem().string()+"-"+std::to_string(number++)+source.extension().string());
    }
    report(progress,Phase::Search,source,0,0);
    if (!already_equal) {
        std::error_code error;
        fs::rename(source,target,error);
        if (!error) return target; // same filesystem: no second allocation or copy
        if (error != std::errc::cross_device_link)
            throw fs::filesystem_error("Cannot move this ISO. Move it outside the app folder with your file manager.",source,target,error);
        copy_verified(source,target,progress);
    }
    // A different filesystem needs a temporary verified copy. An explicit Move
    // request authorizes removing only this selected source, never sibling ISOs.
    report(progress,Phase::Verify,source,original_size,original_size);
    if (fs::file_size(source) != original_size || fs::last_write_time(source) != original_time)
        throw std::runtime_error("Source changed during move. Both files were retained.");
    std::error_code error;
    if (!fs::remove(source,error) || error)
        throw std::runtime_error("The ISO is protected, but its original could not be removed. Both files were retained.");
    return target;
}

std::string startup_disc(const std::string& saved, const fs::path& directory) {
    const auto available = discs(directory);
    for (const auto& disc : available)
        if (disc.path == fs::path(saved)) return saved;
    return available.size() == 1 ? available.front().path.string() : std::string{};
}

namespace {
fs::path active_saves;
bool active_custom = false;
void existing_directory(const fs::path& path) {
    if (!path.is_absolute() || !fs::is_directory(path))
        throw std::runtime_error("Save folder unavailable. Connect its drive or choose another folder in Settings.");
    fs::path prefix;
    for (const auto& part : path) {
        prefix /= part;
        if (fs::is_symlink(prefix)) throw std::runtime_error("Save folder contains a symbolic link.");
    }
}
fs::path save_marker(const fs::path& folder) {
    if (!prepared_paths) throw std::runtime_error("Protected data folder is unavailable.");
    return folder / (".dusklight-saves-" + prepared_paths->root.filename().string());
}
void check_save_target(const fs::path& folder) {
    existing_directory(folder);
    if (requires_move(folder) || inside(folder,prepared_paths->root/"images") ||
        folder==folder.root_path() || folder==fs::path("/data") || folder==fs::path("/mnt"))
        throw std::runtime_error("Choose a save folder outside the application and ISO folders.");
}
bool marker_valid(const fs::path& folder) {
    const auto path=save_marker(folder);
    if (fs::is_symlink(path) || !fs::is_regular_file(path)) return false;
    std::ifstream input(path); std::string line; std::getline(input,line);
    return line=="Dusklight save folder v1";
}
void write_probe(const fs::path& folder) {
    const auto path=folder/(".dusklight-write-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    if (fs::exists(path) || fs::is_symlink(path)) throw std::runtime_error("Cannot check save folder write access.");
    { std::ofstream out(path,std::ios::binary); out<<"write test"; out.close();
      if (!out) { std::error_code ec; fs::remove(path,ec); throw std::runtime_error("Save folder is not writable."); } }
    if (!fs::remove(path)) throw std::runtime_error("Cannot clean up save folder write test.");
}
bool save_artifact(const fs::path& path) {
    const auto name=path.filename().string();
    if (name=="USA" || name=="EUR" || name=="JAP" || name=="backups") return true;
    for (const char* region:{"USA","EUR","JAP"}) for (char slot:{'A','B'}) {
        const auto raw=std::string("MemoryCard")+slot+"."+region+".raw";
        if (name==raw || name==raw+".mods") return true;
    }
    return false;
}
}

bool is_internal_image(const fs::path& path) {
    return prepared_paths && inside(path,prepared_paths->root/"images");
}
fs::path copy_selected(const fs::path& requested,const ProgressCallback& progress) {
    const auto source=requested.lexically_normal();
    if (!source.is_absolute() || fs::is_symlink(source) || !fs::is_regular_file(source))
        throw std::runtime_error("Source data is not a regular file");
    if (!prepared_paths) throw std::runtime_error("Protected disc folder is unavailable");
    if (is_internal_image(source)) return source;
    const auto candidates=discs(source.parent_path(),progress);
    if (std::none_of(candidates.begin(),candidates.end(),[&](const Disc& disc) { return disc.path==source; }))
        throw std::runtime_error("The selected game is not supported by Dusklight.");
    const auto images=prepared_paths->root/"images";
    directory(images);
    auto target=images/source.filename();
    unsigned number=1;
    while (fs::exists(target) || fs::is_symlink(target)) {
        if (fs::is_symlink(target)) throw std::runtime_error("Persistent image is a symbolic link");
        if (fs::is_regular_file(target) && equal_files(source,target,progress)) return target;
        target=images/(source.stem().string()+"-"+std::to_string(number++)+source.extension().string());
    }
    copy_verified(source,target,progress);
    return target;
}

fs::path prepare_save_folder(const fs::path& requested,const fs::path& copy_from,const ProgressCallback& progress) {
    const auto target=requested.lexically_normal();
    check_save_target(target);
    write_probe(target);
    const auto marker=save_marker(target);
    if ((fs::exists(marker) || fs::is_symlink(marker)) && !marker_valid(target))
        throw std::runtime_error("Existing save folder marker is invalid. No files were replaced.");
    std::vector<std::pair<fs::path,fs::path>> files;
    if (!copy_from.empty() && target!=copy_from.lexically_normal()) {
        existing_directory(copy_from);
        if (copy_from.lexically_normal()!=prepared_paths->root/"user" && !marker_valid(copy_from))
            throw std::runtime_error("Save folder unavailable. Connect its drive or choose another folder in Settings.");
        if (inside(target,copy_from) || inside(copy_from,target))
            throw std::runtime_error("Choose a separate save folder, not a parent or child of the current folder.");
        auto add=[&](const fs::directory_entry& entry) {
            report(progress,Phase::Search,entry.path(),files.size(),0);
            if (entry.is_symlink()) throw std::runtime_error("Save data contains a symbolic link.");
            if (!entry.is_regular_file() || image_name(entry.path()) || entry.path().extension()==".importing") return;
            const auto destination=target/entry.path().lexically_relative(copy_from);
            // Check all destination ancestors before reading or writing through them.
            auto parent=destination.parent_path();
            while (parent!=target) {
                if (fs::is_symlink(parent) || (fs::exists(parent) && !fs::is_directory(parent)))
                    throw std::runtime_error("Unsafe destination for save data.");
                parent=parent.parent_path();
            }
            if (fs::is_symlink(destination) || (fs::exists(destination) && !equal_files(entry.path(),destination,progress)))
                throw std::runtime_error("Different saves already exist in this folder. Nothing was overwritten. Choose another folder or use its existing saves.");
            if (!fs::exists(destination)) files.emplace_back(entry.path(),destination);
        };
        for (const auto& entry:fs::directory_iterator(copy_from)) {
            if (!save_artifact(entry.path())) continue;
            if (entry.is_symlink()) throw std::runtime_error("Save data contains a symbolic link.");
            if (entry.is_directory()) for (const auto& child:fs::recursive_directory_iterator(entry.path())) add(child);
            else add(entry);
        }
        // Preflight conflicts first. Interrupted copies never change the active setting.
        for (const auto& [source,destination]:files) copy_verified(source,destination,progress);
    }
    report(progress,Phase::Verify,target,1,1);
    if (!fs::exists(marker)) {
        std::ofstream out(marker); out<<"Dusklight save folder v1\n"; out.close();
        if (!out) throw std::runtime_error("Cannot register this save folder.");
    }
    return target;
}

void activate_save_folder(const std::string& configured) {
    active_saves.clear(); active_custom=!configured.empty();
    if (!prepared_paths) throw std::runtime_error("Protected data folder is unavailable.");
    const auto target=active_custom?fs::path(configured).lexically_normal():prepared_paths->root/"user";
    check_save_target(target);
    if (active_custom && !marker_valid(target))
        throw std::runtime_error("Save folder unavailable. Connect its drive or choose it again in Settings.");
    write_probe(target);
    active_saves=target;
}
fs::path save_folder() { return active_saves; }
bool save_folder_ready() noexcept {
    try {
        if (active_saves.empty()) return false;
        existing_directory(active_saves);
        return !active_custom || marker_valid(active_saves);
    } catch (...) { return false; }
}
fs::path card_path(const std::string& game,bool raw,int channel) {
    if (!save_folder_ready() || game.size()!=4 || channel<0 || channel>1)
        throw std::runtime_error("Save folder unavailable. Connect its drive or choose another folder in Settings.");
    const char* region=game[3]=='P'?"EUR":game[3]=='E'?"USA":game[3]=='J'?"JAP":nullptr;
    if (!region) throw std::runtime_error("Unsupported save region.");
    return raw?active_saves/(std::string("MemoryCard")+(channel?'B':'A')+"."+region+".raw"):
        active_saves/region/(channel?"Card B":"Card A");
}
}
