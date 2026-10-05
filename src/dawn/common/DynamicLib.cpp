// Copyright 2017 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/dawn/common/DynamicLib.h"

#include <span>
#include <utility>

#include "src/utils/platform.h"

#if DAWN_PLATFORM_IS(WINDOWS)
#include "src/utils/windows_with_undefs.h"
#if DAWN_PLATFORM_IS(WINUWP)
#include "src/dawn/common/WindowsUtils.h"
#endif
#elif (DAWN_PLATFORM_IS(POSIX) && !DAWN_PLATFORM_IS(PS5))
#include <dlfcn.h>
#elif (DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5))
#else
#error "Unsupported platform for DynamicLib"
#endif

namespace dawn {
namespace {

#if DAWN_PLATFORM_IS(WINDOWS) && !DAWN_PLATFORM_IS(WINUWP) && \
    !defined(DAWN_FORCE_SYSTEM_COMPONENT_LOAD)
bool IsWindowsPathSeparator(char character) {
    return character == '\\' || character == '/';
}

bool IsAsciiAlpha(char character) {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
}

bool IsFullyQualifiedWindowsPath(const std::string& path) {
    const bool isDrivePath = path.size() >= 3 && IsAsciiAlpha(path[0]) && path[1] == ':' &&
                             IsWindowsPathSeparator(path[2]);
    const bool isUncPath =
        path.size() >= 2 && IsWindowsPathSeparator(path[0]) && IsWindowsPathSeparator(path[1]);
    return isDrivePath || isUncPath;
}
#endif

}  // anonymous namespace

DynamicLib::~DynamicLib() {
    Close();
}

DynamicLib::DynamicLib(DynamicLib&& other) {
    std::swap(mHandle, other.mHandle);
    mNeedsClose = other.mNeedsClose;
}

DynamicLib& DynamicLib::operator=(DynamicLib&& other) {
    std::swap(mHandle, other.mHandle);
    mNeedsClose = other.mNeedsClose;
    return *this;
}

bool DynamicLib::Valid() const {
    return mHandle != nullptr;
}

#if DAWN_PLATFORM_IS(WINDOWS) && !DAWN_PLATFORM_IS(WINUWP)
bool DynamicLib::OpenSystemLibrary(std::wstring_view filename, std::string* error) {
    DAWN_ASSERT(mHandle == nullptr);

    // Force LOAD_LIBRARY_SEARCH_SYSTEM32 for system libraries to avoid DLL search path
    // attacks.
    mHandle = ::LoadLibraryExW(filename.data(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (mHandle == nullptr && error != nullptr) {
        *error = "Windows Error: " + std::to_string(GetLastError());
    }

    mNeedsClose = mHandle != nullptr;
    return mNeedsClose;
}
#endif

bool DynamicLib::Open(const std::string& filename, std::string* error) {
    DAWN_ASSERT(mHandle == nullptr);

#if DAWN_PLATFORM_IS(WINDOWS)
#if DAWN_PLATFORM_IS(WINUWP)
    mHandle = LoadPackagedLibrary(UTF8ToWStr(filename.c_str()).c_str(), 0);
#else
#if defined(DAWN_FORCE_SYSTEM_COMPONENT_LOAD)
    const DWORD loadLibraryFlags = LOAD_LIBRARY_SEARCH_SYSTEM32;
#else
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR requires a fully qualified path. Bare library names use
    // the secure default search directories without searching the current working directory.
    DWORD loadLibraryFlags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    if (IsFullyQualifiedWindowsPath(filename)) {
        loadLibraryFlags |= LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
    }
#endif
    mHandle = LoadLibraryExA(filename.c_str(), nullptr, loadLibraryFlags);
#endif
    if (mHandle == nullptr && error != nullptr) {
        *error =
            "DynamicLib.Open: " + filename + " Windows Error: " + std::to_string(GetLastError());
    }
#elif (DAWN_PLATFORM_IS(POSIX) && !DAWN_PLATFORM_IS(PS5))
    mHandle = dlopen(filename.c_str(), RTLD_NOW);

    if (mHandle == nullptr && error != nullptr) {
        *error = dlerror();
    }
#elif (DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5))
    (void)filename;
    if (error != nullptr) {
        *error = "Dynamic library loading is unavailable on this platform";
    }
    mHandle = nullptr;
#else
#error "Unsupported platform for DynamicLib"
#endif

    mNeedsClose = mHandle != nullptr;
    return mNeedsClose;
}

// Opens a dynamic library that has already been loaded into the process.
bool DynamicLib::OpenLoaded(const std::string& filename, std::string* error) {
    DAWN_ASSERT(mHandle == nullptr);

#if DAWN_PLATFORM_IS(WINDOWS)
    mHandle = GetModuleHandleA(filename.c_str());
    if (mHandle == nullptr && error != nullptr) {
        *error = "DynamicLib.OpenLoaded: " + filename +
                 " Windows Error: " + std::to_string(GetLastError());
    }
#elif (DAWN_PLATFORM_IS(POSIX) && !DAWN_PLATFORM_IS(PS5))
    mHandle = dlopen(filename.c_str(), RTLD_NOW | RTLD_NOLOAD);

    if (mHandle == nullptr && error != nullptr) {
        *error = dlerror();
    }
#elif (DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5))
    (void)filename;
    if (error != nullptr) {
        *error = "Dynamic library loading is unavailable on this platform";
    }
    mHandle = nullptr;
#else
#error "Unsupported platform for DynamicLib"
#endif

    mNeedsClose = false;

    return mHandle != nullptr;
}

bool DynamicLib::Open(const std::string& filename,
                      std::span<const std::string> searchPaths,
                      std::string* error) {
    for (const std::string& path : searchPaths) {
        const std::string fullPath = path + filename;
        if (Open(fullPath, error)) {
            return true;
        }
    }
    return false;
}

void DynamicLib::Close() {
    if (mHandle == nullptr) {
        return;
    }

    // If the dynamic library was opened with OpenLoaded don't attempt to close it.
    if (mNeedsClose) {
#if DAWN_PLATFORM_IS(WINDOWS)
#if !DAWN_ASAN_ENABLED()
        // Freeing and reloading a DLL on Windows causes ASAN to detect ODR violations.
        // https://github.com/google/sanitizers/issues/89
        // In ASAN builds, we have to leak the DLL instead in case it gets loaded again later.
        FreeLibrary(static_cast<HMODULE>(mHandle));
#endif
#elif (DAWN_PLATFORM_IS(POSIX) && !DAWN_PLATFORM_IS(PS5))
        dlclose(mHandle);
#elif (DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5))
#else
#error "Unsupported platform for DynamicLib"
#endif
    }

    mNeedsClose = false;
    mHandle = nullptr;
}

void* DynamicLib::GetProc(const std::string& procName, std::string* error) const {
    void* proc = nullptr;

#if DAWN_PLATFORM_IS(WINDOWS)
    proc = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(mHandle), procName.c_str()));

    if (proc == nullptr && error != nullptr) {
        *error = "Windows Error: " + std::to_string(GetLastError());
    }
#elif (DAWN_PLATFORM_IS(POSIX) && !DAWN_PLATFORM_IS(PS5))
    proc = reinterpret_cast<void*>(dlsym(mHandle, procName.c_str()));

    if (proc == nullptr && error != nullptr) {
        *error = dlerror();
    }
#elif (DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5))
    (void)procName;
    if (error != nullptr) {
        *error = "Dynamic symbol lookup is unavailable on Switch";
    }
#else
#error "Unsupported platform for DynamicLib"
#endif

    return proc;
}

}  // namespace dawn
