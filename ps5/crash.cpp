// SPDX-License-Identifier: MIT
// Native PS5 termination uses the shell. Signal/GPU faults remain visible in
// kernel logs; Linux dl_iterate_phdr/backtrace handlers cannot run in this title.
#include <cstdio>
#include <exception>

extern "C" int sceSystemServiceLoadExec(const char*, const char* const*);
extern "C" int sceKernelUsleep(unsigned int);

namespace borealis::crash {
void install() {
    std::set_terminate([] {
        std::fputs("[Twilight PS5] Unhandled C++ exception; closing through system service\n", stderr);
        std::fflush(nullptr);
        sceSystemServiceLoadExec("exit", nullptr);
        for (;;) sceKernelUsleep(100000);
    });
}
}
