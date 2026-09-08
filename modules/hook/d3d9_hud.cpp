// filepath: modules/hook/d3d9_hud.cpp
// Direct in-game HUD overlay rendered to Direct3D 9 backbuffer.

#include "d3d9_hud.h"
#include "d3d9_font.h"
#include "d3d9_post.h"
#include "d3d9_shared_surfaces.h"

#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

namespace omnirender::hook {

namespace {

bool g_hud_visible = true;
bool g_hud_stats   = false;

float g_fps = 60.0f;
float g_frame_time_ms = 16.6f;
uint64_t g_frame_count = 0;
auto g_last_time = std::chrono::steady_clock::now();
auto g_fps_update_time = std::chrono::steady_clock::now();
uint32_t g_frames_since_fps = 0;

void UpdateFPS() {
    g_frame_count++;
    g_frames_since_fps++;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float, std::milli>(now - g_last_time).count();
    g_last_time = now;
    g_frame_time_ms = g_frame_time_ms * 0.9f + dt * 0.1f;

    float elapsed_sec = std::chrono::duration<float>(now - g_fps_update_time).count();
    if (elapsed_sec >= 0.25f) {
        g_fps = static_cast<float>(g_frames_since_fps) / elapsed_sec;
        g_frames_since_fps = 0;
        g_fps_update_time = now;
    }
}

}  // namespace

void ToggleD3D9HUD() {
    g_hud_visible = !g_hud_visible;
}

void ToggleD3D9HUDStats() {
    g_hud_stats = !g_hud_stats;
}

void RenderD3D9InGameHUD(IDirect3DDevice9* device) {
    if (!device) return;

    static bool s_f11_prev = false;
    bool f11_down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (f11_down && !s_f11_prev) {
        ToggleD3D9HUD();
    }
    s_f11_prev = f11_down;

    static bool s_f12_prev = false;
    bool f12_down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    if (f12_down && !s_f12_prev) {
        ToggleD3D9HUDStats();
    }
    s_f12_prev = f12_down;

    UpdateFPS();

    if (!g_hud_visible) return;

    std::vector<Vertex2D> verts;
    verts.reserve(2048);

    char buf[128];
    snprintf(buf, sizeof(buf), "FPS: %.1f (%.1f ms)", g_fps, g_frame_time_ms);

    bool fx_on = IsPostEnhancementActive();
    bool split_on = IsPostSplitScreenActive();
    int sharp_pct = static_cast<int>(GetPostSharpness() * 100.0f + 0.5f);

    if (!g_hud_stats) {
        AddQuad(verts, 12.0f, 12.0f, 410.0f, 72.0f, D3DCOLOR_ARGB(230, 13, 17, 23));
        AddQuad(verts, 12.0f, 12.0f, 4.0f, 72.0f, D3DCOLOR_ARGB(255, 56, 189, 248));
        AddQuad(verts, 12.0f, 12.0f, 410.0f, 1.0f, D3DCOLOR_ARGB(255, 55, 65, 81));
        AddQuad(verts, 12.0f, 84.0f, 410.0f, 1.0f, D3DCOLOR_ARGB(255, 55, 65, 81));

        char banner_hdr[128];
        snprintf(banner_hdr, sizeof(banner_hdr), "OmniRender [FX:%s Split:%s Sharp:%d%%]",
                 fx_on ? "ON" : "OFF", split_on ? "ON" : "OFF", sharp_pct);

        AddString(verts, banner_hdr, 22.0f, 18.0f, 1.15f, D3DCOLOR_ARGB(255, 56, 189, 248));
        AddString(verts, buf, 22.0f, 36.0f, 1.15f, D3DCOLOR_ARGB(255, 74, 222, 128));
        AddString(verts, "[F8] FX  [F9] Split  [F10] Sharp  [F11] Hide  [F12] Stats", 22.0f, 54.0f, 0.90f, D3DCOLOR_ARGB(255, 156, 163, 175));
    } else {
        AddQuad(verts, 12.0f, 12.0f, 450.0f, 212.0f, D3DCOLOR_ARGB(240, 13, 17, 23));
        AddQuad(verts, 12.0f, 12.0f, 4.0f, 212.0f, D3DCOLOR_ARGB(255, 99, 102, 241));
        AddQuad(verts, 12.0f, 12.0f, 450.0f, 1.0f, D3DCOLOR_ARGB(255, 55, 65, 81));
        AddQuad(verts, 12.0f, 224.0f, 450.0f, 1.0f, D3DCOLOR_ARGB(255, 55, 65, 81));

        AddString(verts, "OmniRender In-Engine Neural Processor", 22.0f, 18.0f, 1.25f, D3DCOLOR_ARGB(255, 129, 140, 248));
        AddString(verts, buf, 22.0f, 36.0f, 1.15f, D3DCOLOR_ARGB(255, 74, 222, 128));

        D3DDEVICE_CREATION_PARAMETERS cp{};
        device->GetCreationParameters(&cp);

        IDirect3D9* d3d = nullptr;
        char gpu_name[64] = "Direct3D 9 Hardware Adapter";
        if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d) {
            D3DADAPTER_IDENTIFIER9 ident{};
            if (SUCCEEDED(d3d->GetAdapterIdentifier(cp.AdapterOrdinal, 0, &ident))) {
                snprintf(gpu_name, sizeof(gpu_name), "%.60s", ident.Description);
            }
            d3d->Release();
        }
        snprintf(buf, sizeof(buf), "GPU: %s", gpu_name);
        AddString(verts, buf, 22.0f, 54.0f, 1.0f, D3DCOLOR_ARGB(255, 229, 231, 235));

        UINT bb_w = 0, bb_h = 0;
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            D3DSURFACE_DESC sd{};
            if (SUCCEEDED(bb->GetDesc(&sd))) {
                bb_w = sd.Width;
                bb_h = sd.Height;
            }
            bb->Release();
        }
        snprintf(buf, sizeof(buf), "Render Target: %ux%u D3D9 -> Enhanced Backbuffer", bb_w, bb_h);
        AddString(verts, buf, 22.0f, 72.0f, 1.0f, D3DCOLOR_ARGB(255, 147, 197, 253));

        UINT vram_mb = device->GetAvailableTextureMem() / (1024 * 1024);
        snprintf(buf, sizeof(buf), "VRAM Available: %u MB", vram_mb);
        AddString(verts, buf, 22.0f, 90.0f, 1.0f, D3DCOLOR_ARGB(255, 209, 213, 219));

        snprintf(buf, sizeof(buf), "Enhancement: OmniSharpen + SDR Tone Map [%s]", fx_on ? "ACTIVE" : "OFF");
        AddString(verts, buf, 22.0f, 108.0f, 1.0f, fx_on ? D3DCOLOR_ARGB(255, 56, 189, 248) : D3DCOLOR_ARGB(255, 156, 163, 175));

        snprintf(buf, sizeof(buf), "Comparison Mode: %s",
                 split_on ? "Split-Screen (Left:Original | Right:Enhanced)" : "Full-Frame Enhanced");
        AddString(verts, buf, 22.0f, 126.0f, 1.0f, split_on ? D3DCOLOR_ARGB(255, 251, 191, 36) : D3DCOLOR_ARGB(255, 209, 213, 219));

        snprintf(buf, sizeof(buf), "Sharpness: %d%% (RCAS-Style Contrast-Adaptive)", sharp_pct);
        AddString(verts, buf, 22.0f, 144.0f, 1.0f, D3DCOLOR_ARGB(255, 167, 139, 250));

        bool ring_active = IsRingMappingActive();
        snprintf(buf, sizeof(buf), "IPC Ring Buffer: %s",
                 ring_active ? "Connected (64-Bit Daemon Active)" : "Standalone In-Engine (D3D9 Direct)");
        AddString(verts, buf, 22.0f, 162.0f, 1.0f, ring_active ? D3DCOLOR_ARGB(255, 52, 211, 153) : D3DCOLOR_ARGB(255, 147, 197, 253));

        AddString(verts, "[F8] FX  [F9] Split  [F10] Sharp  [F11] Hide  [F12] Compact", 22.0f, 184.0f, 0.90f, D3DCOLOR_ARGB(255, 156, 163, 175));
    }

    if (verts.empty()) return;

    IDirect3DStateBlock9* state_block = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &state_block))) {
        state_block = nullptr;
    }
    if (state_block) state_block->Capture();

    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetFVF(kFontFVF);
    device->SetTexture(0, nullptr);

    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    if (SUCCEEDED(device->BeginScene())) {
        device->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(verts.size() / 3),
            verts.data(),
            sizeof(Vertex2D));
        device->EndScene();
    }

    if (state_block) {
        state_block->Apply();
        state_block->Release();
    }
}

}  // namespace omnirender::hook
