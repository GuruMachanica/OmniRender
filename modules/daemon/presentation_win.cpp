// filepath: modules/daemon/presentation_win.cpp
// Borderless flip-model overlay window with low-latency waitable swapchain presentation.

#include <d3d11.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <string>

#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "../common/shared_fence.h"
#include "frame_profiler.h"
#include "hud_overlay.h"
#include "interop_d3d11.h"
#include "ipc_server.h"
#include "pipeline.h"
#include "pipeline_runtime.h"
#include "presentation_win.h"
#include "processing.h"

namespace omnirender::daemon {

namespace {

HWND                    g_hwnd                   = nullptr;
IDXGISwapChain2*        g_swapchain              = nullptr;
ID3D11RenderTargetView* g_rtv                    = nullptr;
HANDLE                  g_frame_latency_waitable = nullptr;
std::atomic<bool>       g_running                { true };

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_F11) {
            ToggleHudVisibility();
            return 0;
        }
        if (wp == VK_F12) {
            CycleDebugMode();
            return 0;
        }
    }
    if (msg == WM_DESTROY || msg == WM_CLOSE) {
        g_running.store(false, std::memory_order_release);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

}  // namespace

IDXGISwapChain2*        SwapChain()        { return g_swapchain; }
ID3D11RenderTargetView* RenderTargetView() { return g_rtv; }
HWND                    OverlayWindow()    { return g_hwnd; }
void                    RequestStop()      { g_running.store(false, std::memory_order_release); }

bool CreateOverlayWindow(HINSTANCE hInstance, UINT width, UINT height) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OmniRenderOverlay";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    if (width == 0)  width  = GetSystemMetrics(SM_CXSCREEN);
    if (height == 0) height = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"OmniRender", WS_POPUP,
        0, 0, width, height, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return false;
    ShowWindow(g_hwnd, SW_SHOW);
    return true;
}

bool CreateSwapChain(UINT width, UINT height) {
    if (!Device()) return false;

    if (g_frame_latency_waitable) {
        CloseHandle(g_frame_latency_waitable);
        g_frame_latency_waitable = nullptr;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width              = width  ? width  : 1280;
    desc.Height             = height ? height : 720;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount        = 2;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags              = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    IDXGIDevice*   dxgi_device  = nullptr;
    IDXGIAdapter*  dxgi_adapter = nullptr;
    IDXGIFactory2* dxgi_factory = nullptr;
    if (FAILED(Device()->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device)))) return false;
    if (FAILED(dxgi_device->GetAdapter(&dxgi_adapter))) { dxgi_device->Release(); return false; }
    if (FAILED(dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgi_factory)))) {
        dxgi_adapter->Release(); dxgi_device->Release(); return false;
    }

    HRESULT hr = dxgi_factory->CreateSwapChainForHwnd(
        Device(), g_hwnd, &desc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&g_swapchain));
    dxgi_factory->Release(); dxgi_adapter->Release(); dxgi_device->Release();
    if (FAILED(hr)) return false;

    IDXGISwapChain2* sc2 = nullptr;
    if (SUCCEEDED(g_swapchain->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(&sc2)))) {
        sc2->SetMaximumFrameLatency(1);
        g_frame_latency_waitable = sc2->GetFrameLatencyWaitableObject();
        sc2->Release();
    }

    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer)))) return false;
    Device()->CreateRenderTargetView(backbuffer, nullptr, &g_rtv);
    backbuffer->Release();

    InitializeHudOverlay(Device());
    return true;
}

void BlitFrame(ID3D11ShaderResourceView* source_srv, UINT width, UINT height) {
    if (!source_srv || !Context()) return;
    PresentProcessed(Context(), source_srv, width ? width : 1280, height ? height : 720, false);
}

int InitializePresentation() {
    HINSTANCE h = ::GetModuleHandleW(nullptr);
    if (!CreateOverlayWindow(h, 0, 0)) return -1;
    if (!InitializeInterop()) return -2;
    if (!CreateSwapChain(1280, 720)) return -3;
    g_running.store(true, std::memory_order_release);
    return 0;
}

void ShutdownPresentation() {
    ShutdownHudOverlay();
    if (g_frame_latency_waitable) { CloseHandle(g_frame_latency_waitable); g_frame_latency_waitable = nullptr; }
    if (g_rtv)       { g_rtv->Release();       g_rtv = nullptr; }
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_hwnd)      { ::DestroyWindow(g_hwnd); g_hwnd = nullptr; }
}

int RunPresentationLoop() {
    if (InitializePresentation() != 0) return 3;

    uint64_t last_frame_index = 0;
    UINT last_width = 1280, last_height = 720;
    MSG msg{};

    while (g_running.load(std::memory_order_acquire)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running.store(false, std::memory_order_release); break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!g_running.load(std::memory_order_acquire)) break;

        if (GetAsyncKeyState(VK_F11) & 0x0001) ToggleHudVisibility();
        if (GetAsyncKeyState(VK_F12) & 0x0001) CycleDebugMode();

        omnirender::FrameSlot* slot = nullptr;
        if (ConsumeFrame(slot) && slot) {
            if (slot->payload.surface_width != last_width || slot->payload.surface_height != last_height) {
                if (g_rtv)       { g_rtv->Release();       g_rtv = nullptr; }
                if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
                if (!CreateSwapChain(slot->payload.surface_width, slot->payload.surface_height)) {
                    ReleaseFrame(*slot);
                    continue;
                }
                last_width = slot->payload.surface_width; last_height = slot->payload.surface_height;
            }

            omnirender::FenceWait waiter;
            waiter.WaitForFence(*slot, slot->payload.frame_index, 100);

#ifndef OMNIRENDER_LEGACY_PIPELINE
            if (RuntimePipelineReady()) {
                if (NewPipelineFrame(*slot) < 0) RunPassthroughFrame(*slot);
            } else {
                RunPassthroughFrame(*slot);
            }
#else
            if (PipelineReady()) {
                if (RunPipelineFrame(*slot) < 0) RunPassthroughFrame(*slot);
            } else {
                RunPassthroughFrame(*slot);
            }
#endif

            if (IsHudVisible() && Context() && g_rtv) {
                Context()->OMSetRenderTargets(1, &g_rtv, nullptr);
                std::wstring hw = GetGlobalProfiler().FormatHudText();
                std::string hs;
                hs.reserve(hw.size());
                for (wchar_t c : hw) hs.push_back(static_cast<char>(c & 0x7F));
                RenderHudOverlay(Context(), hs, 16, 16, (int)last_width, (int)last_height);
            }

            if (g_swapchain) {
                if (g_frame_latency_waitable) WaitForSingleObjectEx(g_frame_latency_waitable, 1000, TRUE);
                g_swapchain->Present(0, 0);
            }

            last_frame_index = slot->payload.frame_index;
            ReleaseFrame(*slot);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ShutdownPresentation();
    return static_cast<int>(msg.wParam);
}

}  // namespace omnirender::daemon
