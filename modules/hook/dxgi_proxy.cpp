// filepath: modules/hook/dxgi_proxy.cpp
// DXGI drop-in proxy exports for dxgi.dll hooking.
// Kept separate from dxgi_interceptor.cpp so that file stays under 300 LOC.

#include <windows.h>

namespace {

HMODULE GetRealDXGIModule() {
    static HMODULE s_real_dxgi = nullptr;
    if (!s_real_dxgi) {
        wchar_t sys_path[MAX_PATH]{};
        ::GetSystemDirectoryW(sys_path, MAX_PATH);
        wcscat_s(sys_path, L"\\dxgi.dll");
        s_real_dxgi = ::LoadLibraryW(sys_path);
    }
    return s_real_dxgi;
}

}  // namespace

using PFN_CreateDXGIFactory  = HRESULT (WINAPI *)(REFIID, void**);
using PFN_CreateDXGIFactory1 = HRESULT (WINAPI *)(REFIID, void**);
using PFN_CreateDXGIFactory2 = HRESULT (WINAPI *)(UINT, REFIID, void**);

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** pp) {
    HMODULE m = GetRealDXGIModule();
    if (!m) return E_FAIL;
    auto pfn = reinterpret_cast<PFN_CreateDXGIFactory>(::GetProcAddress(m, "CreateDXGIFactory"));
    return pfn ? pfn(riid, pp) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** pp) {
    HMODULE m = GetRealDXGIModule();
    if (!m) return E_FAIL;
    auto pfn = reinterpret_cast<PFN_CreateDXGIFactory1>(::GetProcAddress(m, "CreateDXGIFactory1"));
    return pfn ? pfn(riid, pp) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT f, REFIID riid, void** pp) {
    HMODULE m = GetRealDXGIModule();
    if (!m) return E_FAIL;
    auto pfn = reinterpret_cast<PFN_CreateDXGIFactory2>(::GetProcAddress(m, "CreateDXGIFactory2"));
    return pfn ? pfn(f, riid, pp) : E_FAIL;
}

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:CreateDXGIFactory=_Proxy_CreateDXGIFactory@8")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory1=_Proxy_CreateDXGIFactory1@8")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory2=_Proxy_CreateDXGIFactory2@12")
#else
#pragma comment(linker, "/EXPORT:CreateDXGIFactory=Proxy_CreateDXGIFactory")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory1=Proxy_CreateDXGIFactory1")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory2=Proxy_CreateDXGIFactory2")
#endif
