// filepath: core/temporal/ShaderPath.h
// Executable-relative shader path helper for temporal compute passes.
// The daemon CMake compiles shaders into the binary directory and copies
// them alongside the executable at POST_BUILD.  This header provides a
// platform-portable function to locate a CSO by name relative to the
// running executable rather than the working directory.
#pragma once

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace omnirender::core::temporal {

// Returns the absolute path to <name>.cso located next to the running
// executable (e.g. C:\Program Files\OmniRender\MotionReproject.cso).
// Falls back to the working directory if the executable path cannot be
// determined, so in-source dev builds still work.
inline std::string GetShaderPath(const char* filename) {
#if defined(_WIN32)
    char exe_path[MAX_PATH] = {};
    DWORD len = ::GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return filename; // fallback

    std::string path(exe_path, len);
    auto last_sep = path.find_last_of("\\/");
    if (last_sep != std::string::npos)
        path = path.substr(0, last_sep + 1);
    else
        path.clear();

    return path + filename;
#else
    // Non-Windows build: no executable-relative lookup is available, so the
    // optional GPU accelerator is simply never found and the CPU path is used.
    (void)filename;
    return "";
#endif
}

}  // namespace omnirender::core::temporal
