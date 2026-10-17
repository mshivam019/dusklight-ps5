#include "ps5_disc_picker.hpp"
#ifdef __PROSPERO__
#include "localization.hpp"
#include "modal.hpp"
#include "format.hpp"
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
bool scan_open = false;
using Selected = std::function<void(std::string)>;

void show_choices(std::vector<ps5_storage::Disc> discs, const std::string& error, Selected selected) {
    Modal::Props props{.title = "Select Disc Image"};
    if (!error.empty()) {
        props.bodyText = localization::translate(error);
        props.icon = "warning";
    } else if (discs.empty()) {
        const char* folder = std::getenv("DUSK_PS5_IMAGES");
        props.bodyText = localization::translate("No disc image found.") + "\n" +
            std::string(folder ? folder : "") + "\n/mnt/usb0\n/mnt/usb1";
    } else {
        props.bodyText = "Choose a GameCube disc image. A changed disc takes effect after restarting Dusklight.";
        for (const auto& disc : discs) {
            props.actions.push_back({
                .label = disc.path.filename().string()+" ("+disc.region+")",
                .onPressed = [path=disc.path.string(),selected](Modal& modal) {
                    modal.pop();
                    selected(path);
                },
            });
        }
    }
    props.actions.push_back({.label="Cancel",.onPressed=[](Modal& modal) { modal.pop(); }});
    props.isVertical = true;
    if (auto* host = top_document()) host->push(std::make_unique<Modal>(std::move(props)));
}

class ScanWindow final : public WindowSmall {
public:
    explicit ScanWindow(Selected selected) : WindowSmall("modal"), mSelected(std::move(selected)) {
        auto* header = append(mDialog,"modal-header");
        append_text(append(header,"modal-title"),"Preparing disc selection");
        auto* icon = append(header,"icon");
        icon->SetClass("verifying",true);
        auto* body = append(mDialog,"modal-body");
        append_text(append(body,"div"),"Your menu stays visible while files are checked. Original files are retained.");
        auto* content = append(body,"verification-progress");
        mStage = append(content,"div");
        mFile = append(content,"file-path");
        mBar = append(content,"progress");
        mBar->SetClass("info",true);
        mDetail = append(content,"small");
        set_text_content(mStage,"Searching disc images...");
        mBar->SetAttribute("value",0.f);
        auto* actions = append(mDialog,"modal-actions");
        mCancelButton = std::make_unique<Button>(actions,"Cancel");
        mCancelButton->root()->SetClass("modal-btn",true);
        mCancelButton->on_pressed([this] { cancel(); });
    }
    ~ScanWindow() override {
        mCancel.store(true,std::memory_order_relaxed);
        if (mWorker.joinable()) mWorker.join();
        if (!mFinished) scan_open = false;
    }
    void update() override {
        WindowSmall::update();
        if (mFinished) return;
        if (mCancel.load(std::memory_order_relaxed) && !mWorker.joinable()) {
            finish(false);
            return;
        }
        // Allow the window/background to paint before starting disk I/O.
        if (++mFrames == 2) {
            try {
                mWorker = std::thread([this] {
                    try {
                        if (const char* error = std::getenv("DUSK_PS5_STORAGE_ERROR"))
                            throw std::runtime_error(error);
                        mDiscs = ps5_storage::scan([this](const ps5_storage::Progress& value) {
                            if (mCancel.load(std::memory_order_relaxed)) return false;
                            std::lock_guard guard(mMutex);
                            mProgress = value;
                            return true;
                        },{"/mnt/usb0","/mnt/usb1"});
                    } catch (const std::exception& error) {
                        mError = error.what();
                    } catch (...) {
                        mError = "Unable to read the selected file.";
                    }
                    mDone.store(true,std::memory_order_release);
                });
            } catch (const std::exception& error) {
                mError = error.what();
                mDone.store(true,std::memory_order_release);
            }
        }
        if (mDone.load(std::memory_order_acquire)) {
            if (mWorker.joinable()) mWorker.join();
            finish(!mCancel.load(std::memory_order_relaxed));
            return;
        }
        if (mCancel.load(std::memory_order_relaxed)) return;
        ps5_storage::Progress state;
        { std::lock_guard guard(mMutex); state = mProgress; }
        const char* stage = state.phase == ps5_storage::Phase::Copy ? "Backing up ISO..." :
            state.phase == ps5_storage::Phase::Verify ? "Verifying backup..." : "Searching disc images...";
        set_text_content(mStage,stage);
        set_text_content(mFile,state.file);
        if (state.total) {
            const auto fraction = std::clamp(float(state.completed)/float(state.total),0.f,1.f);
            mBar->SetAttribute("value",fraction);
            if (state.phase == ps5_storage::Phase::Search) {
                set_text_content(mDetail,localization::translate("Files checked")+": "+
                    fmt::format("{} / {}",state.completed,state.total));
            } else {
                constexpr ByteFormat format{.gibFractionDigits=2,.mibFractionDigits=0};
                set_text_content(mDetail,fmt::format("{} / {} ({:.0f}%)",
                    format_bytes(state.completed,format),format_bytes(state.total,format),fraction*100.f));
            }
        } else {
            // Directory enumeration has no known total. Animate a pulse and do
            // not display a fabricated percentage until a real total is known.
            mBar->SetAttribute("value",0.15f+0.7f*float(mFrames%120)/120.f);
            set_text_content(mDetail,"Searching disc images...");
        }
    }
    bool focus() override { return mCancelButton->focus(); }
protected:
    bool handle_nav_command(Rml::Event& event, NavCommand command) override {
        if (command == NavCommand::Cancel || command == NavCommand::Menu) {
            cancel(); event.StopPropagation(); return true;
        }
        return false;
    }
private:
    void cancel() {
        mCancel.store(true,std::memory_order_relaxed);
        mCancelButton->set_text("Cancelling...");
        mCancelButton->set_disabled(true);
    }
    void finish(bool show_result) {
        mFinished = true;
        scan_open = false;
        pop();
        if (show_result) show_choices(std::move(mDiscs),mError,mSelected);
    }
    Selected mSelected;
    std::thread mWorker;
    std::mutex mMutex;
    ps5_storage::Progress mProgress;
    std::vector<ps5_storage::Disc> mDiscs;
    std::string mError;
    std::atomic_bool mDone{false},mCancel{false};
    bool mFinished = false;
    unsigned mFrames = 0;
    Rml::Element *mStage=nullptr,*mFile=nullptr,*mBar=nullptr,*mDetail=nullptr;
    std::unique_ptr<Button> mCancelButton;
};
}

void open_ps5_disc_picker(Selected selected) {
    if (scan_open) return;
    if (auto* host = top_document()) {
        auto window = std::make_unique<ScanWindow>(std::move(selected));
        scan_open = true;
        host->push(std::move(window));
    }
}
}
#endif
