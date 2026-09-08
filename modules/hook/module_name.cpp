// filepath: modules/hook/module_name.cpp
// Helper for the DllMain entry point to detect whether the DLL is
// being loaded as opengl32.dll (the OpenGL proxy rename).
//
// In Windows the module filename is fetched via GetModuleFileNameW
// on a handle to the DLL itself. When the loader renames the file
// to opengl32.dll, the basename matches "opengl32.dll" and we
// know we are acting as the OpenGL proxy.

#include <windows.h>
#include <string>
#include "../common/logging.h"

namespace omnirender::hook {

bool LoadedAsOpenGLProxy(HMODULE hModule) {
    if (!hModule) return false;
    wchar_t path[MAX_PATH]{};
    if (::GetModuleFileNameW(hModule, path, MAX_PATH) == 0) return false;
    // Find the last backslash or forward slash.
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    // Case-insensitive compare against L"opengl32.dll".
    return (_wcsicmp(base, L"opengl32.dll") == 0);
}

bool IsProxyDll(HMODULE hModule) {
    if (!hModule) return false;
    wchar_t path[MAX_PATH]{};
    if (::GetModuleFileNameW(hModule, path, MAX_PATH) == 0) return false;
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    return (_wcsicmp(base, L"d3d9.dll") == 0 ||
            _wcsicmp(base, L"dxgi.dll") == 0 ||
            _wcsicmp(base, L"opengl32.dll") == 0);
}

}  // namespace omnirender::hook
