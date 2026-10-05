#include <cstdlib>
#include <SDL3/SDL_stdinc.h>
#include "native/platform.h"
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
    SDL_SetEnvironmentVariable(environment, "XDG_DATA_HOME", "/app0/user", true);
    char app[] = "/app0/eboot.bin";
    char user[] = "--user-dir=/app0/user";
    char log[] = "--log-dir=/app0/user/logs";
    char disc[] = "/app0/game.iso";
    char* args[] = {app, user, log, disc, nullptr};
    return aurora_main(4, args);
}
