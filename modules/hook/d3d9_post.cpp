// filepath: modules/hook/d3d9_post.cpp
// In-engine Direct3D 9 post-processing implementation (OmniSharpen + SDR Tone Map).

#include "d3d9_post.h"
#include "d3d9_post_bytecode.h"

#include <windows.h>
#include <d3d9.h>

namespace omnirender::hook {

namespace {

struct PostVertex {
    float x, y, z, rhw;
    float u, v;
};
constexpr DWORD kPostFVF = D3DFVF_XYZRHW | D3DFVF_TEX1;

bool g_enhancement_active = true;
bool g_split_screen       = false;
float g_sharpness         = 0.70f;

IDirect3DDevice9*      g_post_device = nullptr;
IDirect3DTexture9*     g_post_tex    = nullptr;
UINT                   g_post_w      = 0;
UINT                   g_post_h      = 0;
IDirect3DPixelShader9* g_post_ps     = nullptr;

bool EnsurePostResources(IDirect3DDevice9* device, UINT width, UINT height) {
    if (!device) return false;

    if (g_post_device != device) {
        ResetD3D9PostProcessing();
        g_post_device = device;
    }

    if (!g_post_ps) {
        HRESULT hr = device->CreatePixelShader(kPostShaderBytecode, &g_post_ps);
        if (FAILED(hr) || !g_post_ps) return false;
    }

    if (!g_post_tex || g_post_w != width || g_post_h != height) {
        if (g_post_tex) {
            g_post_tex->Release();
            g_post_tex = nullptr;
        }
        HRESULT hr = device->CreateTexture(width, height, 1,
                                           D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                           D3DPOOL_DEFAULT, &g_post_tex, nullptr);
        if (FAILED(hr) || !g_post_tex) return false;
        g_post_w = width;
        g_post_h = height;
    }

    return true;
}

void PollPostHotkeys() {
    static bool s_f8_prev = false, s_f9_prev = false, s_f10_prev = false;
    bool f8_down  = (GetAsyncKeyState(VK_F8)  & 0x8000) != 0;
    bool f9_down  = (GetAsyncKeyState(VK_F9)  & 0x8000) != 0;
    bool f10_down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;

    if (f8_down && !s_f8_prev) {
        g_enhancement_active = !g_enhancement_active;
    }
    if (f9_down && !s_f9_prev) {
        g_split_screen = !g_split_screen;
    }
    if (f10_down && !s_f10_prev) {
        if (g_sharpness < 0.5f) g_sharpness = 0.70f;
        else if (g_sharpness < 0.85f) g_sharpness = 1.00f;
        else g_sharpness = 0.35f;
    }
    s_f8_prev = f8_down;
    s_f9_prev = f9_down;
    s_f10_prev = f10_down;
}

}  // namespace

bool IsPostEnhancementActive() { return g_enhancement_active; }
bool IsPostSplitScreenActive() { return g_split_screen; }
float GetPostSharpness()       { return g_sharpness; }

void ResetD3D9PostProcessing() {
    if (g_post_tex) {
        g_post_tex->Release();
        g_post_tex = nullptr;
    }
    g_post_w = 0;
    g_post_h = 0;
    if (g_post_ps) {
        g_post_ps->Release();
        g_post_ps = nullptr;
    }
    g_post_device = nullptr;
}

void ApplyD3D9PostProcessing(IDirect3DDevice9* device) {
    if (!device) return;

    PollPostHotkeys();

    if (!g_enhancement_active && !g_split_screen) return;

    IDirect3DSurface9* bb = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) {
        return;
    }
    D3DSURFACE_DESC desc{};
    if (FAILED(bb->GetDesc(&desc))) {
        bb->Release();
        return;
    }

    // Capture state block BEFORE any GPU operation (including StretchRect)
    IDirect3DStateBlock9* sb = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &sb))) {
        sb = nullptr;
    }
    if (sb) sb->Capture();

    if (!EnsurePostResources(device, desc.Width, desc.Height)) {
        if (sb) { sb->Apply(); sb->Release(); }
        bb->Release();
        return;
    }

    IDirect3DSurface9* tex_surf = nullptr;
    if (FAILED(g_post_tex->GetSurfaceLevel(0, &tex_surf)) || !tex_surf) {
        if (sb) { sb->Apply(); sb->Release(); }
        bb->Release();
        return;
    }
    HRESULT hr_copy = device->StretchRect(bb, nullptr, tex_surf, nullptr, D3DTEXF_NONE);
    tex_surf->Release();
    if (FAILED(hr_copy)) {
        if (sb) { sb->Apply(); sb->Release(); }
        bb->Release();
        return;
    }

    device->SetRenderTarget(0, bb);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);

    device->SetTexture(0, g_post_tex);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    float w = static_cast<float>(desc.Width);
    float h = static_cast<float>(desc.Height);

    float c_params[4] = { g_sharpness, 1.0f, g_split_screen ? 1.0f : 0.0f, 0.0f };
    float c_res[4]    = { 1.0f / w, 1.0f / h, w, h };
    device->SetPixelShaderConstantF(0, c_params, 1);
    device->SetPixelShaderConstantF(1, c_res, 1);

    device->SetVertexShader(nullptr);
    device->SetPixelShader(g_post_ps);
    device->SetFVF(kPostFVF);

    PostVertex verts[6] = {
        { -0.5f,     -0.5f,     0.0f, 1.0f, 0.0f, 0.0f },
        { w - 0.5f,  -0.5f,     0.0f, 1.0f, 1.0f, 0.0f },
        { w - 0.5f,  h - 0.5f,  0.0f, 1.0f, 1.0f, 1.0f },
        { -0.5f,     -0.5f,     0.0f, 1.0f, 0.0f, 0.0f },
        { w - 0.5f,  h - 0.5f,  0.0f, 1.0f, 1.0f, 1.0f },
        { -0.5f,     h - 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
    };

    // Safe BeginScene / EndScene guard
    if (SUCCEEDED(device->BeginScene())) {
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, verts, sizeof(PostVertex));
        device->EndScene();
    }

    device->SetTexture(0, nullptr);
    device->SetPixelShader(nullptr);

    if (sb) {
        sb->Apply();
        sb->Release();
    }
    bb->Release();
}

}  // namespace omnirender::hook
