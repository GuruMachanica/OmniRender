// filepath: modules/hook/dxgi_interceptor.cpp
// DirectX 10/11 DXGI Present hook.
//
// Hooks IDXGISwapChain::Present. Each captured present is keyed-mutex-
// synchronized and copied into a D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX texture.
// CaptureFrameDXGI is split into dxgi_capture_frame.cpp for LOC compliance.

#define DXGI_DEFINE_GLOBALS
#include "dxgi_shared_state.h"
#undef DXGI_DEFINE_GLOBALS

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>

#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "../common/shared_fence.h"
#include "../common/vtable_hook.h"
#include "dxgi_depth_capture.h"

namespace omnirender::hook {

namespace {

using PFN_DXGISwapChain_Present = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
struct OriginalDXGI { PFN_DXGISwapChain_Present Present = nullptr; };
OriginalDXGI g_original;

// CaptureFrameDXGI defined in dxgi_capture_frame.cpp.
void CaptureFrameDXGI(IDXGISwapChain* swap);

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* self, UINT sync, UINT flags) {
    if (self) CaptureFrameDXGI(self);
    return g_original.Present(self, sync, flags);
}

void OnSwapChainCreated(IDXGISwapChain* swap) {
    if (!swap || g_original.Present) return;
    void** vtable = *reinterpret_cast<void***>(swap);
    g_original.Present = reinterpret_cast<PFN_DXGISwapChain_Present>(vtable[8]);
    omnirender::InstallVTableHook(vtable,
        {{ 8, reinterpret_cast<void*>(&HookedPresent) }}, &g_original);
}

}  // namespace

namespace dxgi_state {

// Define globals (declared extern in dxgi_shared_state.h for other TUs).
omnirender::RingControlBlock* g_ring              = nullptr;
ID3D11Device*                g_d3d11_device       = nullptr;
ID3D11DeviceContext*         g_d3d11_context      = nullptr;
ID3D11Texture2D*             g_shared_color_tex   = nullptr;
ID3D11Texture2D*             g_shared_depth_tex   = nullptr;
HANDLE                       g_shared_color_handle = nullptr;
HANDLE                       g_shared_depth_handle = nullptr;
uint32_t                     g_tex_width           = 0;
uint32_t                     g_tex_height          = 0;
DXGI_FORMAT                  g_tex_format          = DXGI_FORMAT_UNKNOWN;

void EnsureRingMapping() noexcept {
    if (g_ring) return;
    HANDLE mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
                                        omnirender::kIPCBlockName);
    if (!mapping) {
        mapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, sizeof(omnirender::RingControlBlock),
                                       omnirender::kIPCBlockName);
    }
    if (!mapping) return;
    g_ring = static_cast<omnirender::RingControlBlock*>(
        ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                        sizeof(omnirender::RingControlBlock)));
    if (!g_ring) return;
    uint32_t expected = 0;
    if (g_ring->magic.compare_exchange_strong(expected, omnirender::kIpcMagic,
                                              std::memory_order_acq_rel)) {
        g_ring->capacity.store(omnirender::kRingCapacity, std::memory_order_release);
        for (auto& slot : g_ring->slots)
            slot.state.store(static_cast<uint32_t>(omnirender::SlotState::Free),
                             std::memory_order_release);
    }
}

bool EnsureSharedTextures(IDXGISwapChain* swap) noexcept {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap->GetDesc(&desc))) return false;
    const uint32_t W = desc.BufferDesc.Width;
    const uint32_t H = desc.BufferDesc.Height;
    const DXGI_FORMAT F = desc.BufferDesc.Format;

    if (g_shared_color_tex && W == g_tex_width && H == g_tex_height && F == g_tex_format)
        return true;

    if (g_shared_color_tex) { g_shared_color_tex->Release(); g_shared_color_tex = nullptr; }
    if (g_shared_depth_tex) { g_shared_depth_tex->Release(); g_shared_depth_tex = nullptr; }
    g_shared_color_handle = nullptr;
    g_shared_depth_handle = nullptr;
    ResetTrackedState();

    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device),
                               reinterpret_cast<void**>(&g_d3d11_device)))) return false;
    g_d3d11_device->GetImmediateContext(&g_d3d11_context);
    InstallContextHooks(g_d3d11_context);

    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = W; sd.Height = H; sd.MipLevels = 1; sd.ArraySize = 1;
    sd.Format = F; sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_DEFAULT; sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sd.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    if (FAILED(g_d3d11_device->CreateTexture2D(&sd, nullptr, &g_shared_color_tex)))
        return false;
    IDXGIResource* res = nullptr;
    if (SUCCEEDED(g_shared_color_tex->QueryInterface(__uuidof(IDXGIResource),
                                                     reinterpret_cast<void**>(&res)))) {
        res->GetSharedHandle(&g_shared_color_handle); res->Release();
    }

    sd.Format = DXGI_FORMAT_R32_FLOAT;
    if (SUCCEEDED(g_d3d11_device->CreateTexture2D(&sd, nullptr, &g_shared_depth_tex))) {
        IDXGIResource* dr = nullptr;
        if (SUCCEEDED(g_shared_depth_tex->QueryInterface(__uuidof(IDXGIResource),
                                                          reinterpret_cast<void**>(&dr)))) {
            dr->GetSharedHandle(&g_shared_depth_handle); dr->Release();
        }
        IDXGIKeyedMutex* km = omnirender::GetKeyedMutex(g_shared_depth_tex);
        if (km) { km->AcquireSync(omnirender::kKeyedMutexProducer, INFINITE); km->Release(); }
    }

    IDXGIDevice* dxgi_dev = nullptr;
    if (SUCCEEDED(g_d3d11_device->QueryInterface(__uuidof(IDXGIDevice),
                                                  reinterpret_cast<void**>(&dxgi_dev)))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC adesc{};
            if (SUCCEEDED(adapter->GetDesc(&adesc))) {
                uint64_t luid = (static_cast<uint64_t>(adesc.AdapterLuid.HighPart) << 32)
                                | adesc.AdapterLuid.LowPart;
                if (g_ring) g_ring->adapter_luid.store(luid, std::memory_order_release);
            }
            adapter->Release();
        }
        dxgi_dev->Release();
    }

    g_tex_width = W; g_tex_height = H; g_tex_format = F;
    OMNI_LOG_INFO("DXGI shared textures ready: %ux%u fmt=%u", W, H, static_cast<uint32_t>(F));
    return true;
}

}  // namespace dxgi_state

void InstallDXGIInterceptors() {
    WNDCLASSEXW wc{ sizeof(wc), 0, ::DefWindowProcW, 0, 0,
                    ::GetModuleHandleW(nullptr), nullptr, nullptr, nullptr,
                    nullptr, L"OmniHookDummy", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
                                  0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1; sd.BufferDesc.Width = 100; sd.BufferDesc.Height = 100;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;

    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* dev = nullptr; IDXGISwapChain* swap = nullptr;
    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                                 0, &fl, 1, D3D11_SDK_VERSION,
                                                 &sd, &swap, &dev, nullptr, nullptr)) && swap) {
        OnSwapChainCreated(swap); swap->Release();
    }
    if (dev) dev->Release();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    OMNI_LOG_INFO("DXGI interceptors initialized");
}

}  // namespace omnirender::hook
