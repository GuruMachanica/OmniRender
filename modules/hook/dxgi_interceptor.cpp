// filepath: modules/hook/dxgi_interceptor.cpp
// DirectX 10/11 DXGI Present hook.
//
// Hooks IDXGISwapChain::Present on the host's swap chain. Each captured
// present is copied into a D3D11 texture created with
// D3D11_RESOURCE_MISC_SHARED. The daemon opens the resulting NT
// handle via ID3D11Device::OpenSharedResource with zero copy.

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>

#include "../common/halton.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "../common/vtable_hook.h"

namespace omnirender::hook {

namespace {

using PFN_DXGISwapChain_Present = HRESULT (STDMETHODCALLTYPE *)(
    IDXGISwapChain*, UINT, UINT);

struct OriginalDXGI {
    PFN_DXGISwapChain_Present Present = nullptr;
};

OriginalDXGI g_original;

ID3D11Device*        g_d3d11_device        = nullptr;
ID3D11DeviceContext* g_d3d11_context       = nullptr;
ID3D11Texture2D*     g_shared_color_tex    = nullptr;
ID3D11Texture2D*     g_shared_depth_tex    = nullptr;
HANDLE               g_shared_color_handle = nullptr;
HANDLE               g_shared_depth_handle = nullptr;

omnirender::RingControlBlock* g_ring = nullptr;

// Forward declaration so HookedPresent can call CaptureFrameDXGI
// even though it is defined further down in this TU.
void CaptureFrameDXGI(IDXGISwapChain* swap);

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* self, UINT sync, UINT flags) {
    if (self) {
        CaptureFrameDXGI(self);
    }
    return g_original.Present(self, sync, flags);
}

void EnsureRingMapping() {
    if (g_ring) return;
    HANDLE mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
                                        omnirender::kIPCBlockName);
    if (!mapping) {
        mapping = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(omnirender::RingControlBlock),
            omnirender::kIPCBlockName);
    }
    if (!mapping) return;
    g_ring = static_cast<omnirender::RingControlBlock*>(
        ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                        sizeof(omnirender::RingControlBlock)));
    if (!g_ring) return;
    uint32_t expected = 0;
    if (g_ring->magic.compare_exchange_strong(
            expected, omnirender::kIpcMagic,
            std::memory_order_acq_rel)) {
        g_ring->capacity.store(omnirender::kRingCapacity, std::memory_order_release);
        for (auto& slot : g_ring->slots) {
            slot.state.store(static_cast<uint32_t>(omnirender::SlotState::Free),
                             std::memory_order_release);
        }
    }
}

bool EnsureSharedTextures(IDXGISwapChain* swap) {
    if (g_shared_color_tex) return true;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_d3d11_device)))) {
        return false;
    }
    g_d3d11_device->GetImmediateContext(&g_d3d11_context);

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap->GetDesc(&desc))) return false;

    D3D11_TEXTURE2D_DESC shared_desc{};
    shared_desc.Width              = desc.BufferDesc.Width;
    shared_desc.Height             = desc.BufferDesc.Height;
    shared_desc.MipLevels          = 1;
    shared_desc.ArraySize          = 1;
    shared_desc.Format             = desc.BufferDesc.Format;
    shared_desc.SampleDesc.Count   = 1;
    shared_desc.Usage              = D3D11_USAGE_DEFAULT;
    shared_desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
    shared_desc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;
    shared_desc.CPUAccessFlags     = 0;

    if (FAILED(g_d3d11_device->CreateTexture2D(&shared_desc, nullptr, &g_shared_color_tex))) {
        return false;
    }
    IDXGIResource* res = nullptr;
    if (SUCCEEDED(g_shared_color_tex->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&res)))) {
        res->GetSharedHandle(&g_shared_color_handle);
        res->Release();
    }

    shared_desc.Format = DXGI_FORMAT_R32_FLOAT;
    if (SUCCEEDED(g_d3d11_device->CreateTexture2D(&shared_desc, nullptr, &g_shared_depth_tex))) {
        IDXGIResource* dres = nullptr;
        if (SUCCEEDED(g_shared_depth_tex->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&dres)))) {
            dres->GetSharedHandle(&g_shared_depth_handle);
            dres->Release();
        }
    }

    IDXGIDevice* dxgi_dev = nullptr;
    if (SUCCEEDED(g_d3d11_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_dev)))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC adesc{};
            if (SUCCEEDED(adapter->GetDesc(&adesc))) {
                uint64_t luid = (static_cast<uint64_t>(adesc.AdapterLuid.HighPart) << 32) | adesc.AdapterLuid.LowPart;
                if (g_ring) g_ring->adapter_luid.store(luid, std::memory_order_release);
            }
            adapter->Release();
        }
        dxgi_dev->Release();
    }

    OMNI_LOG_INFO("DXGI shared textures ready: %ux%u",
                  desc.BufferDesc.Width, desc.BufferDesc.Height);
    return true;
}

void CaptureFrameDXGI(IDXGISwapChain* swap) {
    if (!g_ring) EnsureRingMapping();
    if (!g_ring || !EnsureSharedTextures(swap)) return;

    omnirender::FrameSlot* slot = nullptr;
    for (uint32_t i = 0; i < omnirender::kRingCapacity; ++i) {
        auto state = omnirender::GetState(g_ring->slots[i]);
        if (state == omnirender::SlotState::Free) {
            omnirender::SetState(g_ring->slots[i], omnirender::SlotState::Captured);
            slot = &g_ring->slots[i];
            break;
        }
    }
    if (!slot) return;

    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                               reinterpret_cast<void**>(&backbuffer)))) {
        omnirender::SetState(*slot, omnirender::SlotState::Free);
        return;
    }
    g_d3d11_context->CopyResource(g_shared_color_tex, backbuffer);
    backbuffer->Release();

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    slot->payload.magic_header        = omnirender::kIpcMagic;
    slot->payload.frame_index         = ++g_ring->producer_seq;
    slot->payload.surface_width       = desc.BufferDesc.Width;
    slot->payload.surface_height      = desc.BufferDesc.Height;
    slot->payload.target_width        = desc.BufferDesc.Width;
    slot->payload.target_height       = desc.BufferDesc.Height;
    slot->payload.color_format        = static_cast<uint32_t>(desc.BufferDesc.Format);
    slot->payload.depth_format        = 0x00000029;  // R32_FLOAT
    slot->payload.shared_color_handle = reinterpret_cast<uint64_t>(g_shared_color_handle);
    slot->payload.shared_depth_handle = reinterpret_cast<uint64_t>(g_shared_depth_handle);
    slot->payload.camera_near         = 0.1f;
    slot->payload.camera_far          = 1000.0f;
    slot->payload.fov_vertical_rad    = 1.0471975512f;
    omnirender::Halton23 jitter       = omnirender::Halton23At(slot->payload.frame_index);
    slot->payload.jitter_x            = jitter.x;
    slot->payload.jitter_y            = jitter.y;
    // Zero matrices: the DXGI path doesn't yet extract the game
    // camera; the daemon falls back to disagreement-mask reconstruction.
    for (int i = 0; i < 16; ++i) {
        slot->payload.view_proj_current[i]  = 0.0f;
        slot->payload.view_proj_previous[i] = 0.0f;
    }
    slot->payload.motion_format       = 0x00000022;  // DXGI_FORMAT_R16G16_FLOAT
    slot->payload.struct_version      = omnirender::kIpcVersion_V040;
    slot->payload.flags               = 0;

    slot->fence.store(slot->payload.frame_index, std::memory_order_release);
    omnirender::SetState(*slot, omnirender::SlotState::Ready);
}

void OnSwapChainCreated(IDXGISwapChain* swap) {
    if (!swap || g_original.Present) return;
    void** vtable = *reinterpret_cast<void***>(swap);
    g_original.Present = reinterpret_cast<PFN_DXGISwapChain_Present>(vtable[8]);
    omnirender::InstallVTableHook(vtable, {
        { 8, reinterpret_cast<void*>(&HookedPresent) },
    }, &g_original);
}

}  // namespace

void InstallDXGIInterceptors() {
    WNDCLASSEXW wc{ sizeof(wc), 0, ::DefWindowProcW, 0, 0, ::GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"OmniHookDummy", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swap = nullptr;

    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &swap, &dev, nullptr, &ctx)) && swap) {
        OnSwapChainCreated(swap);
        swap->Release();
    }
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    OMNI_LOG_INFO("DXGI interceptors initialized");
}

}  // namespace omnirender::hook

// --- Proxy Exports for Drop-In dxgi.dll Hooking ---
using PFN_CreateDXGIFactory  = HRESULT (WINAPI *)(REFIID, void**);
using PFN_CreateDXGIFactory1 = HRESULT (WINAPI *)(REFIID, void**);
using PFN_CreateDXGIFactory2 = HRESULT (WINAPI *)(UINT, REFIID, void**);

static HMODULE GetRealDXGIModule() {
    static HMODULE s_real_dxgi = nullptr;
    if (!s_real_dxgi) {
        wchar_t sys_path[MAX_PATH]{};
        ::GetSystemDirectoryW(sys_path, MAX_PATH);
        wcscat_s(sys_path, L"\\dxgi.dll");
        s_real_dxgi = ::LoadLibraryW(sys_path);
    }
    return s_real_dxgi;
}

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

