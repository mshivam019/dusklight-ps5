#include "ps5_storage_ui.hpp"
#ifdef __PROSPERO__

#include "list.hpp"
#include "window.hpp"
#include "format.hpp"
#include "dusk/settings.h"
#if __has_include("ps5_storage.h")
#include "ps5_storage.h"
#else
#include "storage.h"
#endif
#include <fmt/format.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace dusk::ui {
namespace {
namespace fs = std::filesystem;
bool browser_open = false;
using Selected = std::function<void(std::string)>;

struct Row {
    fs::path path;
    bool directory = false;
    std::string label;
};

class FileBrowser final : public WindowSmall {
public:
    explicit FileBrowser(Selected selected, bool saves=false, bool canCopy=true, fs::path initialIso={})
        : WindowSmall("modal"), mSelected(std::move(selected)), mSaves(saves), mCanCopy(canCopy) {
        mRoot->SetProperty("width","80vw");
        mRoot->SetProperty("max-width","1000dp");
        auto* header = append(mDialog,"modal-header");
        append_text(append(header,"modal-title"),mSaves?"Save folder":"Select Disc Image");
        auto* body = append(mDialog,"modal-body");
        mHelp = append(body,"div");
        mPath = append(body,"file-path");
        mPath->SetProperty("word-break","break-all");
        mStage = append(body,"div");
        mBar = append(body,"progress");
        mBar->SetClass("info",true);
        mDetail = append(body,"small");
        mList = std::make_unique<List>(body,List::Props{
            .items={}, .onPressed=[this](std::uint64_t key) { choose(key); },
        });
        mList->root()->SetProperty("height","34vh");
        mList->root()->SetProperty("flex","0 0 auto");
        auto* actions = append(mDialog,"modal-actions");
        actions->SetProperty("flex-wrap","wrap");
        mPlacesButton = button(actions,"Locations",[this] { if (!mBusy) locations(); });
        mUpButton = button(actions,"Back",[this] { if (!mBusy) back(); });
        mMoveButton = button(actions,"Move and use ISO",[this] {
            if (!mBusy && !mAwaitMove.empty()) begin(Mode::Move,mAwaitMove);
        });
        mCopyButton = button(actions,"Copy to internal SSD and use",[this] {
            if (!mBusy && !mAwaitMove.empty()) begin(Mode::Copy,mAwaitMove);
        });
        mDirectButton = button(actions,"Use directly",[this] {
            if (!mBusy && !mAwaitMove.empty()) { const auto path=mAwaitMove.string(); auto selected=mSelected; close(); selected(path); }
        });
        mUseFolderButton = button(actions,"Use this folder",[this] { if (!mBusy && !mFolder.empty()) begin(Mode::SaveExisting,mFolder); });
        mCopySavesButton = button(actions,"Copy current saves and use",[this] {
            if (!mBusy && mCanCopy && !mFolder.empty()) begin(Mode::SaveCopy,mFolder);
        });
        mCancelButton = button(actions,"Cancel",[this] { cancel(); });
        if(initialIso.empty()) locations();
        else {mFolder=initialIso.parent_path();mRows.push_back({initialIso,false,initialIso.filename().string()});show_rows();choose(0);}
    }
    ~FileBrowser() override {
        mCancel.store(true,std::memory_order_relaxed);
        if (mWorker.joinable()) mWorker.join();
        if (!mFinished) browser_open = false;
    }
    void update() override {
        WindowSmall::update();
        if (mFinished) return;
        mList->update();
        if (!mBusy) return;
        if (mCancel.load(std::memory_order_relaxed) && !mWorker.joinable()) { close(); return; }
        // Paint the menu and browser before accessing a possibly slow drive.
        if (mQueued && ++mFrames >= 2) {
            mQueued = false;
            const auto mode=mMode;
            const auto path=mTaskPath;
            try {
                mWorker=std::thread([this,mode,path] {
                    try {
                        const auto progress=[this](const ps5_storage::Progress& value) {
                            if (mCancel.load(std::memory_order_relaxed)) return false;
                            std::lock_guard guard(mMutex); mProgress=value; return true;
                        };
                        if (mode==Mode::Browse) mResultRows=ps5_storage::browse(path,progress);
                        else if (mode==Mode::Move) mResultPath=ps5_storage::move_selected(path,progress);
                        else if (mode==Mode::Copy) mResultPath=ps5_storage::copy_selected(path,progress);
                        else mResultPath=ps5_storage::prepare_save_folder(path,
                            mode==Mode::SaveCopy?ps5_storage::save_folder():fs::path{},progress);
                    } catch (const std::exception& error) { mError=error.what(); }
                      catch (...) { mError="Unable to read the selected file."; }
                    mDone.store(true,std::memory_order_release);
                });
            } catch (const std::exception& error) {
                mError=error.what(); mDone.store(true,std::memory_order_release);
            }
        }
        ++mPulse;
        if (mDone.load(std::memory_order_acquire)) {
            if (mWorker.joinable()) mWorker.join();
            mBusy=false;
            if (mCancel.load(std::memory_order_relaxed)) { close(); return; }
            if (mError.empty() && mMode!=Mode::Browse) {
                auto selected=mSelected;
                const auto path=mResultPath.string();
                close(); selected(path); return;
            }
            if (mMode==Mode::Browse && mError.empty()) {
                mRows.clear();
                for (const auto& entry:mResultRows) if (!mSaves || entry.directory) mRows.push_back({entry.path,entry.directory,
                    entry.path.filename().string()+(entry.directory?"/":"")});
                show_rows();
            } else {
                show_rows();
                set_text_content(mStage,mMode==Mode::Browse?"Cannot open this folder.":"Operation failed. Original files were retained.");
                set_text_content(mDetail,mError);
            }
            focus();
            return;
        }
        if (mCancel.load(std::memory_order_relaxed)) return;
        ps5_storage::Progress state;
        { std::lock_guard guard(mMutex); state=mProgress; }
        const char* label=state.phase==ps5_storage::Phase::Copy?(mSaves?"Copying saves...":"Copying ISO..."):
            state.phase==ps5_storage::Phase::Verify?"Verifying files...":"Reading folder...";
        set_text_content(mStage,label);
        if (state.total) {
            const auto fraction=std::clamp(float(state.completed)/float(state.total),0.f,1.f);
            mBar->SetAttribute("value",fraction);
            if (state.phase==ps5_storage::Phase::Search)
                set_text_content(mDetail,std::string("Files checked")+": "+fmt::format("{} / {}",state.completed,state.total));
            else {
                constexpr ByteFormat format{.gibFractionDigits=2,.mibFractionDigits=0};
                set_text_content(mDetail,fmt::format("{} / {} ({:.0f}%)",format_bytes(state.completed,format),
                    format_bytes(state.total,format),fraction*100.f));
            }
        } else {
            mBar->SetAttribute("value",0.15f+0.7f*float(mPulse%120)/120.f);
            set_text_content(mDetail,std::string("Files checked")+": "+std::to_string(state.completed));
        }
    }
    bool focus() override {
        if (mBusy) return mCancelButton->focus();
        if (!mAwaitMove.empty()) return mManaged?mMoveButton->focus():mCopyButton->focus();
        return mList->focus() || mUpButton->focus() || mCancelButton->focus();
    }
protected:
    bool handle_nav_command(Rml::Event& event,NavCommand command) override {
        if (command==NavCommand::Cancel || command==NavCommand::Menu) {
            if (mBusy) cancel(); else back();
            event.StopPropagation(); return true;
        }
        // List handles its rows; allow controller movement between the list
        // and the footer, and between visible footer buttons.
        std::vector<Button*> buttons;
        if (mAwaitMove.empty()) buttons.push_back(mPlacesButton.get());
        buttons.push_back(mUpButton.get());
        if (!mAwaitMove.empty()) {
            if (mManaged) buttons.push_back(mMoveButton.get());
            else { buttons.push_back(mCopyButton.get()); buttons.push_back(mDirectButton.get()); }
        }
        if (mSaves && !mFolder.empty()) { buttons.push_back(mUseFolderButton.get()); buttons.push_back(mCopySavesButton.get()); }
        buttons.push_back(mCancelButton.get());
        const auto target=event.GetTargetElement();
        auto current=std::find_if(buttons.begin(),buttons.end(),[target](Button* b) { return b->contains(target); });
        bool handled=false;
        if (command==NavCommand::Down && mList->contains(target)) handled=buttons.front()->focus();
        else if (command==NavCommand::Up && current!=buttons.end() && !mBusy && mAwaitMove.empty()) handled=mList->focus_from(command);
        else if (current!=buttons.end() && (command==NavCommand::Left || command==NavCommand::Right)) {
            const int step=command==NavCommand::Left?-1:1;
            for (int i=int(current-buttons.begin())+step;i>=0 && i<int(buttons.size());i+=step)
                if (buttons[i]->focus()) { handled=true; break; }
        }
        if (handled) event.StopPropagation();
        return handled;
    }
private:
    enum class Mode { Browse,Move,Copy,SaveExisting,SaveCopy };
    std::unique_ptr<Button> button(Rml::Element* parent,const char* text,ButtonCallback pressed) {
        auto result=std::make_unique<Button>(parent,text);
        result->root()->SetClass("modal-btn",true);
        result->on_pressed(std::move(pressed));
        return result;
    }
    void normal_view() {
        mAwaitMove.clear();
        mList->root()->SetProperty("display","flex");
        mBar->SetProperty("display","none");
        mMoveButton->root()->SetProperty("display","none");
        mPlacesButton->root()->SetProperty("display","block");
        mPlacesButton->set_disabled(false);
        mUpButton->set_disabled(mFolder.empty());
        mCopyButton->root()->SetProperty("display","none");
        mDirectButton->root()->SetProperty("display","none");
        mUseFolderButton->root()->SetProperty("display",mSaves && !mFolder.empty()?"block":"none");
        mCopySavesButton->root()->SetProperty("display",mSaves && !mFolder.empty()?"block":"none");
        mCopySavesButton->set_disabled(!mCanCopy || !ps5_storage::save_folder_ready());
        set_text_content(mHelp,mSaves?
            (mCanCopy?"Choose a folder for saves. Use its existing saves or copy your current saves there. Changes require a restart.":
             "Choose a folder for saves. Restart the app before copying saves from a running game."):
            "Open a folder and choose your ISO.");
        set_text_content(mPath,mFolder.empty()?std::string("Locations"):mFolder.string());
    }
    void show_rows() {
        normal_view();
        set_text_content(mStage,""); set_text_content(mDetail,"");
        std::vector<List::Item> items;
        for (std::size_t i=0;i<mRows.size();++i) items.push_back({i,mRows[i].label});
        mList->set_items(std::move(items));
        mList->update();
    }
    void locations() {
        if (mBusy) return;
        mFolder.clear(); mRows.clear();
        auto add=[this](const char* label,const fs::path& path) {
            if (!path.empty()) mRows.push_back({path,true,std::string(label)+"  ("+path.string()+")"});
        };
        const auto last=mSaves?ps5_storage::save_folder():fs::path(getSettings().backend.isoPath.getValue()).parent_path();
        if (!last.empty()) add("Last used folder",last);
        if (!mSaves) if (const char* images=std::getenv("DUSK_PS5_IMAGES")) add("Protected ISOs",images);
        add("USB 0","/mnt/usb0"); add("USB 1","/mnt/usb1");
        add("Data storage","/data");
        const char* app=std::getenv("DUSK_PS5_APP_ROOT");
        add("Application folder",app?app:"/app0");
        add("File system","/");
        show_rows();
    }
    void back() {
        if (!mAwaitMove.empty()) { show_rows(); return; }
        if (mFolder.empty()) { close(); return; }
        if (mFolder==mFolder.root_path()) locations();
        else begin(Mode::Browse,mFolder.parent_path());
    }
    void choose(std::uint64_t key) {
        if (mBusy || key>=mRows.size()) return;
        const auto row=mRows[key];
        if (row.directory) { begin(Mode::Browse,row.path); return; }
        try {
            if (!ps5_storage::is_internal_image(row.path)) {
                mManaged=ps5_storage::requires_move(row.path);
                mAwaitMove=row.path;
                mList->root()->SetProperty("display","none");
                mPlacesButton->root()->SetProperty("display","none");
                mMoveButton->root()->SetProperty("display",mManaged?"block":"none");
                mCopyButton->root()->SetProperty("display",mManaged?"none":"block");
                mDirectButton->root()->SetProperty("display",mManaged?"none":"block");
                mUpButton->set_disabled(false);
                set_text_content(mHelp,mManaged?"This ISO is inside the app folder. Move it outside before using it so a store update cannot remove it. No permanent second copy is needed.":"Copy this ISO to protected internal storage, or use it directly. Copying keeps the original on its drive.");
                set_text_content(mPath,row.path.string());
                focus(); return;
            }
            auto selected=mSelected;
            close(); selected(row.path.string()); // no copy for USB or external folders
        } catch (const std::exception& error) { set_text_content(mStage,error.what()); }
    }
    void begin(Mode mode,const fs::path& requested) {
        if (mBusy) return;
        auto path=requested.lexically_normal();
        while (path!=path.root_path() && path.filename().empty()) path=path.parent_path();
        if (mode==Mode::Browse) { mFolder=path; mRows.clear(); }
        mMode=mode; mTaskPath=path;
        mBusy=true; mQueued=true; mFrames=0; mPulse=0;
        mError.clear(); mResultRows.clear(); mResultPath.clear(); mProgress={};
        mCancel.store(false,std::memory_order_relaxed); mDone.store(false,std::memory_order_relaxed);
        mList->root()->SetProperty("display","none");
        mBar->SetProperty("display","block"); mBar->SetAttribute("value",0.f);
        mMoveButton->root()->SetProperty("display","none");
        mCopyButton->root()->SetProperty("display","none");
        mDirectButton->root()->SetProperty("display","none");
        mUseFolderButton->root()->SetProperty("display","none");
        mCopySavesButton->root()->SetProperty("display","none");
        mPlacesButton->set_disabled(true); mUpButton->set_disabled(true);
        set_text_content(mPath,path.string());
        set_text_content(mStage,mode==Mode::Browse?"Reading folder...":mSaves?"Copying saves...":"Copying ISO...");
        set_text_content(mDetail,"");
        mCancelButton->focus();
    }
    void cancel() {
        if (!mBusy) { close(); return; }
        mCancel.store(true,std::memory_order_relaxed);
        mCancelButton->set_text("Cancelling...");
        mCancelButton->set_disabled(true);
    }
    void close() {
        mFinished=true; browser_open=false; pop();
    }
    Selected mSelected;
    bool mSaves=false,mCanCopy=true,mManaged=false;
    std::thread mWorker;
    std::mutex mMutex;
    ps5_storage::Progress mProgress;
    std::vector<Row> mRows;
    std::vector<ps5_storage::BrowseEntry> mResultRows;
    fs::path mFolder,mTaskPath,mAwaitMove,mResultPath;
    std::string mError;
    std::atomic_bool mDone{false},mCancel{false};
    bool mBusy=false,mQueued=false,mFinished=false;
    Mode mMode=Mode::Browse;
    unsigned mFrames=0,mPulse=0;
    Rml::Element *mHelp=nullptr,*mPath=nullptr,*mStage=nullptr,*mBar=nullptr,*mDetail=nullptr;
    std::unique_ptr<List> mList;
    std::unique_ptr<Button> mPlacesButton,mUpButton,mMoveButton,mCopyButton,mDirectButton,mUseFolderButton,mCopySavesButton,mCancelButton;
};
}

void open_ps5_disc_storage(std::string path, Selected selected) {
    if (browser_open) return;
    if (auto* host=top_document()) {
        auto window=std::make_unique<FileBrowser>(std::move(selected),false,true,fs::path(path));
        browser_open=true; host->push(std::move(window));
    }
}
void open_ps5_save_folder_picker(Selected selected,bool canCopy) {
    if (browser_open) return;
    if (auto* host=top_document()) {
        auto window=std::make_unique<FileBrowser>(std::move(selected),true,canCopy);
        browser_open=true; host->push(std::move(window));
    }
}

}
#endif
