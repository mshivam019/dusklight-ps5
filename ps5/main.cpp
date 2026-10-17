#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <SDL3/SDL_stdinc.h>
#include "native/platform.h"
#include "storage.h"
extern "C" void catchReturnFromMain(int status);
extern "C" [[noreturn]] void __wrap_exit(int status) {
    say("Application exit requested: %d", status);
    catchReturnFromMain(status);
    for (;;) {}
}
extern "C" [[noreturn]] void __wrap_abort() {
    say("Fatal application error; requesting shell closure");
    catchReturnFromMain(1);
    for (;;) {}
}
extern "C" int aurora_main(int argc, char** argv);
int main() {
    platform_init("Twilight Princess");
    SDL_Environment* environment = SDL_GetEnvironment();
    SDL_SetEnvironmentVariable(environment, "SDL_VIDEODRIVER", "dummy", true);
    SDL_SetEnvironmentVariable(environment, "SDL_AUDIODRIVER", "ps5", true);
    // Personal files must survive replacement of the complete catalog app folder.
    std::string user_path;
    try {
        const auto title = ps5_storage::title_id("/app0/sce_sys/param.json");
        const auto root = std::filesystem::path("/data/dusklight") / title;
        const auto protected_data = ps5_storage::prepare({"/app0", root,
            std::filesystem::path("/data/prosperostore/previous") / title, "/download0"});
        user_path = protected_data.user.string();
        setenv("DUSK_PS5_IMAGES", protected_data.images.c_str(), 1);
        if (!protected_data.error.empty()) throw std::runtime_error(protected_data.error);
    } catch (const std::exception& error) {
        say("%s", error.what());
        // Use a temporary configuration so failed protection cannot overwrite old saves.
        user_path = "/download0/dusklight-storage-error";
        setenv("DUSK_PS5_STORAGE_ERROR", error.what(), 1);
    }
    SDL_SetEnvironmentVariable(environment, "XDG_DATA_HOME", user_path.c_str(), true);
    platform_dismiss_splash();
    char app[] = "/app0/eboot.bin";
    std::string user = "--user-dir=" + user_path;
    std::string log = "--log-dir=" + user_path + "/logs";
    // Let Dusklight restore the selected disc instead of overriding it each launch.
    char* args[] = {app, user.data(), log.data(), nullptr};
    return aurora_main(3, args);
}
