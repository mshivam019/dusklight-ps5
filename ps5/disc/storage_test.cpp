// SPDX-License-Identifier: MIT
// Synthetic files only; never connect to a console or read a real game disc.
#include "storage.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
void put(const fs::path& path,const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary) << data;
}
std::string get(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),{}};
}
std::string image(const std::string& id) {
    std::string data(0x1000,'\0');
    data.replace(0,6,id);
    data.replace(0x1c,4,"\xc2\x33\x9f\x3d",4);
    return data;
}
template<class F> void rejects(F action) {
    bool failed=false;
    try { action(); } catch (const std::exception&) { failed=true; }
    assert(failed);
}

int main(int argc,char** argv) {
    assert(argc==3);
    const fs::path root=argv[1], other_filesystem=argv[2];
    const auto payload=image("GZ2P01")+std::string(256*1024,'x');
    const auto usb=root/"usb/games/Zelda";
    put(usb/"PAL.ISO",payload);
    put(usb/"notes.txt","unrelated");
    fs::create_directories(usb/"Subfolder");
    ps5_storage::Paths paths{root/"app/",root/"protected",root/"previous",root/"download"};
    put(paths.app/"user/config.json","language settings");
    put(paths.app/"user/nested/should-not-copy.iso",payload);
    put(paths.app/"user/save.raw","packaged save");
    put(paths.download/"user/save.raw","current save");
    auto ready=ps5_storage::prepare(paths);
    assert(ready.error.empty());
    assert(get(ready.user/"save.raw")=="current save");
    assert(!fs::exists(ready.user/"nested/should-not-copy.iso"));
    assert(get(paths.app/"user/nested/should-not-copy.iso")==payload);
    // Fresh installation: navigate nested external folders without an internal ISO.
    const auto rows=ps5_storage::browse(usb);
    assert(rows.size()==2 && rows[0].directory && rows[0].path.filename()=="Subfolder");
    assert(!rows[1].directory && rows[1].path==usb/"PAL.ISO");
    assert(!ps5_storage::requires_move(rows[1].path));
    assert(ps5_storage::move_selected(rows[1].path)==rows[1].path);
    assert(fs::is_empty(ready.images) && get(usb/"PAL.ISO")==payload);
    // A missing/unplugged location yields an error, not an empty successful import.
    rejects([&] { ps5_storage::browse(root/"missing-usb"); });
    rejects([&] { ps5_storage::browse(usb,[](const auto&) { return false; }); });
    assert(fs::is_empty(ready.images));
    // No launch, directory listing or scan silently copies an app-local ISO.
    put(paths.app/"game.iso",payload);
    put(paths.app/"sibling.gcm",image("GZ2E01"));
    assert(ps5_storage::requires_move(paths.app/"game.iso"));
    assert(ps5_storage::requires_move(paths.app/"nested/game.iso"));
    assert(ps5_storage::requires_move(paths.previous/"game.iso"));
    assert(!ps5_storage::requires_move(root/"app-other/game.iso"));
    ps5_storage::browse(paths.app);
    assert(ps5_storage::scan({}, {paths.app,usb}).size()==3);
    assert(fs::is_empty(ready.images));
    // Deleting an old internal copy must not cause it to be recreated.
    put(ready.images/"game.iso",payload);
    fs::remove(ready.images/"game.iso");
    assert(ps5_storage::prepare(paths).error.empty());
    ps5_storage::scan({}, {paths.app});
    ps5_storage::browse(paths.app);
    assert(!fs::exists(ready.images/"game.iso"));
    // Only explicit Move acts; cancellation keeps the source and siblings.
    rejects([&] { ps5_storage::move_selected(paths.app/"game.iso",[](const auto&) { return false; }); });
    assert(get(paths.app/"game.iso")==payload && fs::is_empty(ready.images));
    const auto moved=ps5_storage::move_selected(paths.app/"game.iso");
    assert(moved==ready.images/"game.iso" && get(moved)==payload);
    assert(!fs::exists(paths.app/"game.iso") && fs::exists(paths.app/"sibling.gcm"));
    // Same-name different data is never overwritten; equal redundant sources
    // are removed only when the user explicitly requests that move.
    put(paths.app/"game.iso",image("GZ2E01"));
    const auto collision=ps5_storage::move_selected(paths.app/"game.iso");
    assert(collision==ready.images/"game-1.iso" && get(moved)==payload);
    put(paths.app/"game.iso",payload);
    assert(ps5_storage::move_selected(paths.app/"game.iso")==moved);
    assert(!fs::exists(paths.app/"game.iso"));
    put(paths.app/"wrong.iso",image("GZLE01"));
    rejects([&] { ps5_storage::move_selected(paths.app/"wrong.iso"); });
    assert(fs::exists(paths.app/"wrong.iso"));
    fs::create_symlink(usb/"PAL.ISO",paths.app/"linked.iso");
    rejects([&] { ps5_storage::move_selected(paths.app/"linked.iso"); });
    const auto no_links=ps5_storage::browse(paths.app);
    assert(std::none_of(no_links.begin(),no_links.end(),[](const auto& row) { return row.path.filename()=="linked.iso"; }));
    // Complete app replacement preserves moved images and current saves/settings.
    put(ready.user/"save.raw","newest save");
    fs::remove_all(paths.app);
    fs::create_directories(paths.app);
    assert(ps5_storage::prepare(paths).error.empty());
    assert(get(moved)==payload && get(ready.user/"save.raw")=="newest save");
    assert(get(ready.user/"config.json")=="language settings");
    // Supported regions and revision filtering remain unchanged.
    const auto regions=root/"regions";
    for (auto id:{"GZ2P01","GZ2E01","GZ2J01"}) put(regions/(std::string(id)+".iso"),image(id));
    auto revision=image("GZ2P01"); revision[7]=1;
    put(regions/"revision.iso",revision);
    put(regions/"truncated.iso","GZ2P01");
    const auto discs=ps5_storage::discs(regions);
    assert(discs.size()==3);
    assert(ps5_storage::startup_disc(discs[1].path.string(),regions)==discs[1].path.string());
    assert(ps5_storage::startup_disc("missing",regions).empty());
    // Destination symlinks/conflicts must not overwrite unrelated personal data.
    ps5_storage::Paths unsafe{root/"unsafe-app",root/"unsafe-data",root/"none",{}};
    fs::create_directories(root/"outside"); fs::create_directories(unsafe.root);
    fs::create_directory_symlink(root/"outside",unsafe.root/"user");
    put(unsafe.app/"user/save.raw","retain");
    assert(!ps5_storage::prepare(unsafe).error.empty());
    assert(fs::is_empty(root/"outside") && get(unsafe.app/"user/save.raw")=="retain");
    ps5_storage::Paths conflict{root/"conflict-app",root/"conflict-data",root/"none",{}};
    put(conflict.app/"user/save.raw","retain"); fs::create_directories(conflict.root/"user/save.raw");
    assert(!ps5_storage::prepare(conflict).error.empty());
    assert(get(conflict.app/"user/save.raw")=="retain");

    // A real cross-filesystem move needs temporary space. Cancellation during
    // copying OR byte verification leaves its source and no completed target.
    ps5_storage::Paths cross{root/"cross-app",other_filesystem/"protected",root/"none",{}};
    put(cross.app/"chosen.iso",payload);
    const auto protected_cross=ps5_storage::prepare(cross);
    assert(protected_cross.error.empty());
    for (auto phase:{ps5_storage::Phase::Copy,ps5_storage::Phase::Verify}) {
        bool saw=false;
        rejects([&] { ps5_storage::move_selected(cross.app/"chosen.iso",[&](const auto& value) {
            assert(!value.total || value.completed<=value.total);
            if (value.phase==phase && value.completed>0) { saw=true; return false; }
            return true;
        }); });
        assert(saw && get(cross.app/"chosen.iso")==payload);
        assert(!fs::exists(protected_cross.images/"chosen.iso"));
        assert(ps5_storage::browse(protected_cross.images).empty());
    }
    bool copied=false,verified=false;
    const auto finished=ps5_storage::move_selected(cross.app/"chosen.iso",[&](const auto& value) {
        copied |= value.phase==ps5_storage::Phase::Copy && value.completed==payload.size();
        verified |= value.phase==ps5_storage::Phase::Verify && value.completed==payload.size();
        return true;
    });
    assert(copied && verified && get(finished)==payload && !fs::exists(cross.app/"chosen.iso"));
    assert(ps5_storage::browse(protected_cross.images).size()==1);
    put(root/"param.json","{\"titleId\":\"PPSA99640\"}");
    assert(ps5_storage::title_id(root/"param.json")=="PPSA99640");
    put(root/"param.json","{\"titleId\":\"../bad-id\"}");
    rejects([&] { ps5_storage::title_id(root/"param.json"); });
    std::cout << "Fresh nested USB browsing, no automatic ISO copies/recreation, explicit moves, region selection, collisions, update survival, save precedence, symlinks, cross-filesystem progress and cancellation: PASS\n";
    // Explicit internal copy keeps its external source, reuses equal copies,
    // preserves name collisions, and never silently recreates a deleted image.
    assert(ps5_storage::prepare(paths).error.empty());
    put(usb/"explicit.iso",payload);
    const auto copied_iso=ps5_storage::copy_selected(usb/"explicit.iso");
    assert(get(copied_iso)==payload && get(usb/"explicit.iso")==payload);
    assert(ps5_storage::copy_selected(usb/"explicit.iso")==copied_iso);
    assert(ps5_storage::copy_selected(copied_iso)==copied_iso);
    put(copied_iso,"different existing data");
    const auto copy_collision=ps5_storage::copy_selected(usb/"explicit.iso");
    assert(copy_collision!=copied_iso && get(copied_iso)=="different existing data");
    for (auto phase:{ps5_storage::Phase::Copy,ps5_storage::Phase::Verify}) {
        put(usb/"cancel-copy.iso",payload);
        rejects([&] { ps5_storage::copy_selected(usb/"cancel-copy.iso",[&](const auto& value) {
            return value.phase!=phase || value.completed==0;
        }); });
        assert(get(usb/"cancel-copy.iso")==payload && !fs::exists(ready.images/"cancel-copy.iso"));
        assert(!fs::exists(ready.images/"cancel-copy.iso.importing"));
    }
    // Save location applies to raw and GCI saves, all regions and both slots.
    ps5_storage::activate_save_folder("");
    assert(ps5_storage::save_folder()==ready.user && ps5_storage::save_folder_ready());
    assert(ps5_storage::card_path("GZ2P",false,0)==ready.user/"EUR/Card A");
    assert(ps5_storage::card_path("GZ2E",true,1)==ready.user/"MemoryCardB.USA.raw");
    assert(ps5_storage::card_path("GZ2J",false,1)==ready.user/"JAP/Card B");
    put(ready.user/"EUR/Card A/zelda.gci","PAL progress");
    put(ready.user/"MemoryCardB.USA.raw","USA progress");
    put(ready.user/"MemoryCardB.USA.raw.mods/data.bin","mods");
    put(ready.user/"backups/old.dusksave","backup");
    put(ready.user/"EUR/Card A/stray.iso",payload);
    const auto saves=usb/"save-folder"; fs::create_directories(saves);
    ps5_storage::prepare_save_folder(saves,ready.user);
    assert(get(saves/"EUR/Card A/zelda.gci")=="PAL progress");
    assert(get(saves/"MemoryCardB.USA.raw")=="USA progress");
    assert(get(saves/"MemoryCardB.USA.raw.mods/data.bin")=="mods");
    assert(get(saves/"backups/old.dusksave")=="backup");
    assert(!fs::exists(saves/"config.json") && !fs::exists(saves/"EUR/Card A/stray.iso"));
    assert(get(ready.user/"EUR/Card A/zelda.gci")=="PAL progress");
    ps5_storage::activate_save_folder(saves.string());
    assert(ps5_storage::card_path("GZ2P",false,0)==saves/"EUR/Card A");
    put(saves/"EUR/Card A/zelda.gci","different progress");
    put(ready.user/"EUR/Card B/new.gci","new save");
    rejects([&] { ps5_storage::prepare_save_folder(saves,ready.user); });
    assert(get(saves/"EUR/Card A/zelda.gci")=="different progress");
    assert(!fs::exists(saves/"EUR/Card B/new.gci")); // preflight before writing
    ps5_storage::prepare_save_folder(saves); // explicit use of existing saves
    const auto cancel_saves=usb/"cancel-saves"; fs::create_directories(cancel_saves);
    rejects([&] { ps5_storage::prepare_save_folder(cancel_saves,ready.user,[](const auto& value) {
        return value.phase!=ps5_storage::Phase::Copy;
    }); });
    rejects([&] { ps5_storage::activate_save_folder(cancel_saves.string()); }); // not registered
    assert(!ps5_storage::save_folder_ready());
    rejects([&] { ps5_storage::card_path("GZ2P",false,0); }); // no fallback
    ps5_storage::activate_save_folder(saves.string());
    fs::rename(saves,usb/"unplugged-saves"); fs::create_directories(saves);
    assert(!ps5_storage::save_folder_ready()); // empty replacement mount rejected
    rejects([&] { ps5_storage::activate_save_folder(saves.string()); });
    assert(!ps5_storage::save_folder_ready());
    rejects([&] { ps5_storage::prepare_save_folder(paths.app); });
    rejects([&] { ps5_storage::prepare_save_folder(ready.images); });
    const auto linked=usb/"linked-saves"; fs::create_directory_symlink(ready.user,linked);
    rejects([&] { ps5_storage::prepare_save_folder(linked); });
    ps5_storage::activate_save_folder("");
    assert(get(ready.user/"EUR/Card A/zelda.gci")=="PAL progress");
    std::cout << "Explicit SSD copy, save folder activation, missing drives, raw/GCI paths, conflict preflight and cancellation: PASS\n";

    // A completed migration must never resurrect deliberately deleted old saves.
    ps5_storage::Paths once{root/"once-app",root/"once-data",root/"once-previous",{}};
    put(once.app/"user/EUR/Card A/old.gci","legacy save");
    const auto first=ps5_storage::prepare(once);assert(first.error.empty());
    fs::remove(first.user/"EUR/Card A/old.gci");
    assert(ps5_storage::prepare(once).error.empty());
    assert(!fs::exists(first.user/"EUR/Card A/old.gci"));
    // Failed migration is not marked complete, and succeeds after the cause is fixed.
    ps5_storage::Paths retry{root/"retry-app",root/"retry-data",root/"retry-previous",{}};
    put(retry.app/"user/blocked","old");fs::create_directories(retry.root/"user/blocked");
    assert(!ps5_storage::prepare(retry).error.empty());
    assert(!fs::exists(retry.root/".userdata-migration-v1.complete"));
    fs::remove(retry.root/"user/blocked");
    assert(ps5_storage::prepare(retry).error.empty());
    assert(get(retry.root/"user/blocked")=="old");
    std::cout<<"One-time migration and failed-import retry: PASS\n";

}
