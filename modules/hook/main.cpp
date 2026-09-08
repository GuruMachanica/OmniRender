// filepath: modules/hook/main.cpp
// DllMain entry point for the OmniRender injection proxy.
//
// On DLL_PROCESS_ATTACH we disable thread-library calls (we never use
// them in the hook), initialize the IPC client, and install the D3D9
// + DXGI vtable hooks. If the DLL is being loaded as opengl32.dll
// (the proxy rename), we also install the wglSwapBuffers hook.
//
// The actual capture work happens in d3d9_interceptor.cpp,
// dxgi_interceptor.cpp, and opengl_interceptor.cpp.

#include <windows.h>

#include "../common/logging.h"

namespace omnirender::hook {

bool InitializeIpcClient();
void ShutdownIpcClient();
void InstallIPCClient();
void InstallDepthLocator();
void InstallDXGIInterceptors();
void InstallD3D9Interceptors();
bool InstallWGLHook();

// Returns true when the DLL is being loaded as opengl32.dll. We
// detect this by comparing the module filename (the basename of
// the loaded DLL) to "opengl32.dll".
bool LoadedAsOpenGLProxy(HMODULE hModule);
bool IsProxyDll(HMODULE hModule);

}  // namespace omnirender::hook

static HMODULE g_module_handle = nullptr;

extern "C" __declspec(dllexport) BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD   ul_reason_for_call,
    LPVOID  /*lpReserved*/) {
    g_module_handle = hModule;
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            OMNI_LOG_INFO("omnirender-hook attached to process %lu (as %s)",
                          GetCurrentProcessId(),
                          omnirender::hook::LoadedAsOpenGLProxy(hModule) ? "opengl32.dll" : "injection");
            if (!omnirender::hook::InitializeIpcClient()) {
                OMNI_LOG_ERROR("IPC client init failed");
                return FALSE;
            }
            // For drop-in proxy DLLs (d3d9.dll, dxgi.dll, opengl32.dll), the exported
            // proxy functions (Direct3DCreate9, CreateDXGIFactory, wglSwapBuffers) handle
            // hooking when invoked by the game outside DllMain. Never call heavy DirectX
            // creation functions inside DllMain while holding the Windows loader lock.
            if (!omnirender::hook::IsProxyDll(hModule)) {
                omnirender::hook::InstallDepthLocator();
                omnirender::hook::InstallDXGIInterceptors();
                omnirender::hook::InstallD3D9Interceptors();
                omnirender::hook::InstallIPCClient();
            }
            if (omnirender::hook::LoadedAsOpenGLProxy(hModule)) {
                omnirender::hook::InstallWGLHook();
            }
            break;
        case DLL_PROCESS_DETACH:
            OMNI_LOG_INFO("omnirender-hook detaching");
            omnirender::hook::ShutdownIpcClient();
            break;
        default:
            break;
    }
    return TRUE;
}
