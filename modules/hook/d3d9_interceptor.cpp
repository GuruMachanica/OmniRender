// filepath: modules/hook/d3d9_interceptor.cpp
// DirectX 9 vtable hook interception.

#include <windows.h>
#include <d3d9.h>
#include <cstdint>

#include "../common/logging.h"
#include "../common/vtable_hook.h"
#include "d3d9_hud.h"
#include "d3d9_post.h"
#include "d3d9_shared_surfaces.h"

namespace omnirender::hook {

namespace {

// Vtable indices. These are stable across the D3D9 interface version.
constexpr VTableIndex kIDirect3D9_CreateDevice                 = 16;

constexpr VTableIndex kIDirect3DDevice9_Reset                  = 16;
constexpr VTableIndex kIDirect3DDevice9_Present                = 17;
constexpr VTableIndex kIDirect3DDevice9_GetBackBuffer          = 18;
constexpr VTableIndex kIDirect3DDevice9_SetDepthStencilSurface = 26;
constexpr VTableIndex kIDirect3DDevice9_GetDepthStencilSurface = 27;
constexpr VTableIndex kIDirect3DDevice9_EndScene               = 42;

using PFN_Present = HRESULT (STDMETHODCALLTYPE *)(
    IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
using PFN_EndScene = HRESULT (STDMETHODCALLTYPE *)(IDirect3DDevice9*);
using PFN_Reset = HRESULT (STDMETHODCALLTYPE *)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PFN_SetDepthStencilSurface = HRESULT (STDMETHODCALLTYPE *)(
    IDirect3DDevice9*, IDirect3DSurface9*);
using PFN_GetBackBuffer = HRESULT (STDMETHODCALLTYPE *)(
    IDirect3DDevice9*, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
using PFN_GetDepthStencilSurface = HRESULT (STDMETHODCALLTYPE *)(
    IDirect3DDevice9*, IDirect3DSurface9**);
using PFN_CreateDevice = HRESULT (STDMETHODCALLTYPE *)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

struct OriginalD3D9 {
    PFN_Present               Present               = nullptr;
    PFN_EndScene              EndScene              = nullptr;
    PFN_Reset                 Reset                 = nullptr;
    PFN_SetDepthStencilSurface SetDepthStencilSurface = nullptr;
    PFN_GetBackBuffer         GetBackBuffer         = nullptr;
    PFN_GetDepthStencilSurface GetDepthStencilSurface = nullptr;
    PFN_CreateDevice          CreateDevice          = nullptr;
};

OriginalD3D9 g_original;

bool InstallDeviceHooks(IDirect3DDevice9* device);

HRESULT STDMETHODCALLTYPE HookedPresent(
    IDirect3DDevice9* self,
    CONST RECT* src, CONST RECT* dst,
    HWND wnd, CONST RGNDATA* dirty) {
    if (self) {
        EnsureSharedSurfaces(self);
        EnsureRingMapping();
        CaptureD3D9Frame(self);
        ApplyD3D9PostProcessing(self);
        RenderD3D9InGameHUD(self);
    }
    return g_original.Present(self, src, dst, wnd, dirty);
}

HRESULT STDMETHODCALLTYPE HookedEndScene(IDirect3DDevice9* self) {
    return g_original.EndScene(self);
}

HRESULT STDMETHODCALLTYPE HookedReset(
    IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params) {
    DestroySharedSurfaces();
    ResetD3D9PostProcessing();
    HRESULT hr = g_original.Reset(self, params);
    if (SUCCEEDED(hr)) {
        EnsureSharedSurfaces(self);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedSetDepthStencilSurface(
    IDirect3DDevice9* self, IDirect3DSurface9* depth) {
    return g_original.SetDepthStencilSurface(self, depth);
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(
    IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND wnd,
    DWORD flags, D3DPRESENT_PARAMETERS* params,
    IDirect3DDevice9** out_device) {
    HRESULT hr = g_original.CreateDevice(
        d3d, adapter, type, wnd, flags, params, out_device);
    if (SUCCEEDED(hr) && out_device && *out_device) {
        if (InstallDeviceHooks(*out_device)) {
            OMNI_LOG_INFO("D3D9 device hooked (adapter=%u)", adapter);
        }
    }
    return hr;
}

bool InstallDeviceHooks(IDirect3DDevice9* device) {
    if (!device) return false;
    void** vtable = *reinterpret_cast<void***>(device);

    if (vtable[kIDirect3DDevice9_Present] == reinterpret_cast<void*>(&HookedPresent)) {
        return true;
    }

    if (!g_original.Present) {
        g_original.Present                = reinterpret_cast<PFN_Present>(vtable[kIDirect3DDevice9_Present]);
        g_original.EndScene               = reinterpret_cast<PFN_EndScene>(vtable[kIDirect3DDevice9_EndScene]);
        g_original.Reset                  = reinterpret_cast<PFN_Reset>(vtable[kIDirect3DDevice9_Reset]);
        g_original.SetDepthStencilSurface = reinterpret_cast<PFN_SetDepthStencilSurface>(vtable[kIDirect3DDevice9_SetDepthStencilSurface]);
        g_original.GetBackBuffer          = reinterpret_cast<PFN_GetBackBuffer>(vtable[kIDirect3DDevice9_GetBackBuffer]);
        g_original.GetDepthStencilSurface = reinterpret_cast<PFN_GetDepthStencilSurface>(vtable[kIDirect3DDevice9_GetDepthStencilSurface]);
    }

    return omnirender::InstallVTableHook(vtable, {
        { kIDirect3DDevice9_Present,              reinterpret_cast<void*>(&HookedPresent) },
        { kIDirect3DDevice9_EndScene,             reinterpret_cast<void*>(&HookedEndScene) },
        { kIDirect3DDevice9_Reset,                reinterpret_cast<void*>(&HookedReset) },
        { kIDirect3DDevice9_SetDepthStencilSurface, reinterpret_cast<void*>(&HookedSetDepthStencilSurface) },
    });
}

bool InstallD3D9Hooks(IDirect3D9* d3d) {
    if (!d3d) return false;
    void** vtable = *reinterpret_cast<void***>(d3d);
    if (vtable[kIDirect3D9_CreateDevice] == reinterpret_cast<void*>(&HookedCreateDevice)) {
        return true;
    }
    if (!g_original.CreateDevice) {
        g_original.CreateDevice = reinterpret_cast<PFN_CreateDevice>(vtable[kIDirect3D9_CreateDevice]);
    }
    return omnirender::InstallVTableHook(vtable, {
        { kIDirect3D9_CreateDevice, reinterpret_cast<void*>(&HookedCreateDevice) },
    });
}

}  // namespace

void InstallD3D9Interceptors() {
    IDirect3D9* d3d = ::Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        OMNI_LOG_ERROR("Direct3DCreate9 failed");
        return;
    }
    if (!InstallD3D9Hooks(d3d)) {
        OMNI_LOG_ERROR("InstallD3D9Hooks failed");
    }
    d3d->Release();
}

}  // namespace omnirender::hook

// --- Proxy Exports for Drop-In d3d9.dll Hooking ---
using PFN_Direct3DCreate9 = IDirect3D9* (WINAPI *)(UINT);

static HMODULE GetRealD3D9Module() {
    static HMODULE s_real_d3d9 = nullptr;
    if (!s_real_d3d9) {
        wchar_t sys_path[MAX_PATH]{};
        ::GetSystemDirectoryW(sys_path, MAX_PATH);
        wcscat_s(sys_path, L"\\d3d9.dll");
        s_real_d3d9 = ::LoadLibraryW(sys_path);
    }
    return s_real_d3d9;
}

extern "C" IDirect3D9* WINAPI Proxy_Direct3DCreate9(UINT SDKVersion) {
    HMODULE real_dll = GetRealD3D9Module();
    if (!real_dll) return nullptr;
    auto pfn = reinterpret_cast<PFN_Direct3DCreate9>(::GetProcAddress(real_dll, "Direct3DCreate9"));
    if (!pfn) return nullptr;
    IDirect3D9* d3d = pfn(SDKVersion);
    if (d3d) {
        omnirender::hook::InstallD3D9Hooks(d3d);
    }
    return d3d;
}

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:Direct3DCreate9=_Proxy_Direct3DCreate9@4")
#else
#pragma comment(linker, "/EXPORT:Direct3DCreate9=Proxy_Direct3DCreate9")
#endif

// ---------------------------------------------------------------------------
// Remaining d3d9.dll export family. A proxy that exports ONLY
// Direct3DCreate9 breaks any game whose import table also references the
// D3DPERF_* PIX-profiling functions or Direct3DCreate9Ex: the Windows loader
// fails the process before main() with STATUS_ENTRYPOINT_NOT_FOUND — a
// silent death with no dialog. 2005-era engines (Jade/Ubisoft among them)
// commonly carry those imports.
// All forwarders resolve against the real system d3d9.dll and behave as
// pass-throughs; capture hooking happens through Direct3DCreate9 above.
// ---------------------------------------------------------------------------
static FARPROC GetRealD3D9Proc(const char* name) {
    HMODULE real_dll = GetRealD3D9Module();
    return real_dll ? ::GetProcAddress(real_dll, name) : nullptr;
}

extern "C" HRESULT WINAPI Proxy_Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** out) {
    using PFN = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("Direct3DCreate9Ex"));
    if (!pfn) return E_NOTIMPL;
    HRESULT hr = pfn(SDKVersion, out);
    // Hook the Ex path too: Direct3DCreate9Ex-created devices bypass the
    // plain Create9 hook otherwise.
    if (SUCCEEDED(hr) && out && *out) {
        omnirender::hook::InstallD3D9Hooks(*out);
    }
    return hr;
}
extern "C" int WINAPI Proxy_D3DPERF_BeginEvent(DWORD col, LPCWSTR name) {
    using PFN = int (WINAPI*)(DWORD, LPCWSTR);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_BeginEvent"));
    return pfn ? pfn(col, name) : 0;
}
extern "C" int WINAPI Proxy_D3DPERF_EndEvent(void) {
    using PFN = int (WINAPI*)(void);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_EndEvent"));
    return pfn ? pfn() : 0;
}
extern "C" void WINAPI Proxy_D3DPERF_SetMarker(DWORD col, LPCWSTR name) {
    using PFN = void (WINAPI*)(DWORD, LPCWSTR);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_SetMarker"));
    if (pfn) pfn(col, name);
}
extern "C" void WINAPI Proxy_D3DPERF_SetRegion(DWORD col, LPCWSTR name) {
    using PFN = void (WINAPI*)(DWORD, LPCWSTR);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_SetRegion"));
    if (pfn) pfn(col, name);
}
extern "C" BOOL WINAPI Proxy_D3DPERF_QueryRepeatFrame(void) {
    using PFN = BOOL (WINAPI*)(void);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_QueryRepeatFrame"));
    return pfn ? pfn() : FALSE;
}
extern "C" void WINAPI Proxy_D3DPERF_SetOptions(DWORD options) {
    using PFN = void (WINAPI*)(DWORD);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_SetOptions"));
    if (pfn) pfn(options);
}
extern "C" DWORD WINAPI Proxy_D3DPERF_GetStatus(void) {
    using PFN = DWORD (WINAPI*)(void);
    auto pfn = reinterpret_cast<PFN>(GetRealD3D9Proc("D3DPERF_GetStatus"));
    return pfn ? pfn() : 0;
}

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:Direct3DCreate9Ex=_Proxy_Direct3DCreate9Ex@8")
#pragma comment(linker, "/EXPORT:D3DPERF_BeginEvent=_Proxy_D3DPERF_BeginEvent@8")
#pragma comment(linker, "/EXPORT:D3DPERF_EndEvent=_Proxy_D3DPERF_EndEvent@0")
#pragma comment(linker, "/EXPORT:D3DPERF_SetMarker=_Proxy_D3DPERF_SetMarker@8")
#pragma comment(linker, "/EXPORT:D3DPERF_SetRegion=_Proxy_D3DPERF_SetRegion@8")
#pragma comment(linker, "/EXPORT:D3DPERF_QueryRepeatFrame=_Proxy_D3DPERF_QueryRepeatFrame@0")
#pragma comment(linker, "/EXPORT:D3DPERF_SetOptions=_Proxy_D3DPERF_SetOptions@4")
#pragma comment(linker, "/EXPORT:D3DPERF_GetStatus=_Proxy_D3DPERF_GetStatus@0")
#else
#pragma comment(linker, "/EXPORT:Direct3DCreate9Ex=Proxy_Direct3DCreate9Ex")
#pragma comment(linker, "/EXPORT:D3DPERF_BeginEvent=Proxy_D3DPERF_BeginEvent")
#pragma comment(linker, "/EXPORT:D3DPERF_EndEvent=Proxy_D3DPERF_EndEvent")
#pragma comment(linker, "/EXPORT:D3DPERF_SetMarker=Proxy_D3DPERF_SetMarker")
#pragma comment(linker, "/EXPORT:D3DPERF_SetRegion=Proxy_D3DPERF_SetRegion")
#pragma comment(linker, "/EXPORT:D3DPERF_QueryRepeatFrame=Proxy_D3DPERF_QueryRepeatFrame")
#pragma comment(linker, "/EXPORT:D3DPERF_SetOptions=Proxy_D3DPERF_SetOptions")
#pragma comment(linker, "/EXPORT:D3DPERF_GetStatus=Proxy_D3DPERF_GetStatus")
#endif


