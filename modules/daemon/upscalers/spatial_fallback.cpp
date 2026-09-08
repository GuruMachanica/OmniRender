// filepath: modules/daemon/upscalers/spatial_fallback.cpp
// AMD FidelityFX Super Resolution 1.0 (FSR 1: EASU + RCAS) spatial upscaler.

#include "spatial_fallback.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <algorithm>

#include "../../common/logging.h"
#include "../shader_loader.h"

namespace omnirender::daemon::upscaler {

namespace {

ID3D11ComputeShader*        g_easu_shader       = nullptr;
ID3D11ComputeShader*        g_rcas_shader       = nullptr;
ID3D11Buffer*               g_easu_cb           = nullptr;
ID3D11Buffer*               g_rcas_cb           = nullptr;

ID3D11Texture2D*            g_inter_texture     = nullptr;
ID3D11ShaderResourceView*   g_inter_srv         = nullptr;
ID3D11UnorderedAccessView*  g_inter_uav         = nullptr;
UINT                        g_inter_width       = 0;
UINT                        g_inter_height      = 0;

struct alignas(16) FsrEasuConstants {
    float Const0[4];
    float Const1[4];
    float Const2[4];
    float Const3[4];
};

struct alignas(16) FsrRcasConstants {
    float RcasConfig[4]; // Sharpness, Width, Height, Pad
};

static bool EnsureIntermediate(ID3D11Device* device, UINT width, UINT height) {
    if (g_inter_texture && g_inter_width == width && g_inter_height == height) {
        return true;
    }
    if (g_inter_uav)     { g_inter_uav->Release();     g_inter_uav = nullptr; }
    if (g_inter_srv)     { g_inter_srv->Release();     g_inter_srv = nullptr; }
    if (g_inter_texture) { g_inter_texture->Release(); g_inter_texture = nullptr; }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width          = width;
    desc.Height         = height;
    desc.MipLevels      = 1;
    desc.ArraySize      = 1;
    desc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage          = D3D11_USAGE_DEFAULT;
    desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &g_inter_texture);
    if (FAILED(hr)) return false;
    if (FAILED(device->CreateShaderResourceView(g_inter_texture, nullptr, &g_inter_srv))) return false;
    if (FAILED(device->CreateUnorderedAccessView(g_inter_texture, nullptr, &g_inter_uav))) return false;

    g_inter_width  = width;
    g_inter_height = height;
    return true;
}

}  // namespace

bool InitializeSpatialFallback(ID3D11Device* device) {
    if (!device) return false;

    LoadComputeShader(device, L"modules/shaders/fsr_easu.cso", &g_easu_shader);
    LoadComputeShader(device, L"modules/shaders/fsr_rcas.cso", &g_rcas_shader);

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.Usage          = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = 0;

    cb_desc.ByteWidth = sizeof(FsrEasuConstants);
    device->CreateBuffer(&cb_desc, nullptr, &g_easu_cb);

    cb_desc.ByteWidth = sizeof(FsrRcasConstants);
    device->CreateBuffer(&cb_desc, nullptr, &g_rcas_cb);

    if (g_easu_shader && g_rcas_shader) {
        OMNI_LOG_INFO("FSR 1 (EASU + RCAS) upscaler loaded successfully");
        return true;
    }
    OMNI_LOG_WARN("FSR 1 shaders not loaded (CSO unavailable)");
    return false;
}

void ShutdownSpatialFallback() {
    if (g_easu_shader)   { g_easu_shader->Release();   g_easu_shader = nullptr; }
    if (g_rcas_shader)   { g_rcas_shader->Release();   g_rcas_shader = nullptr; }
    if (g_easu_cb)       { g_easu_cb->Release();       g_easu_cb = nullptr; }
    if (g_rcas_cb)       { g_rcas_cb->Release();       g_rcas_cb = nullptr; }
    if (g_inter_uav)     { g_inter_uav->Release();     g_inter_uav = nullptr; }
    if (g_inter_srv)     { g_inter_srv->Release();     g_inter_srv = nullptr; }
    if (g_inter_texture) { g_inter_texture->Release(); g_inter_texture = nullptr; }
    g_inter_width  = 0;
    g_inter_height = 0;
}

bool SpatialFallbackAvailable() noexcept {
    return g_easu_shader != nullptr && g_rcas_shader != nullptr;
}

void DispatchSpatialFallback(ID3D11DeviceContext* ctx,
                             ID3D11ShaderResourceView* source_srv,
                             ID3D11UnorderedAccessView* target_uav,
                             UINT source_width, UINT source_height,
                             UINT target_width, UINT target_height) {
    if (!ctx || !source_srv || !target_uav) return;
    if (!g_easu_shader || !g_rcas_shader || !g_easu_cb || !g_rcas_cb) return;

    ID3D11Device* device = nullptr;
    ctx->GetDevice(&device);
    if (!device) return;
    if (!EnsureIntermediate(device, target_width, target_height)) {
        device->Release();
        return;
    }
    device->Release();

    // 1. Pass 1: EASU (source_srv -> g_inter_uav)
    FsrEasuConstants ec{};
    ec.Const0[0] = static_cast<float>(source_width)  / static_cast<float>(target_width);
    ec.Const0[1] = static_cast<float>(source_height) / static_cast<float>(target_height);
    ec.Const0[2] = 0.5f * ec.Const0[0] - 0.5f;
    ec.Const0[3] = 0.5f * ec.Const0[1] - 0.5f;

    ec.Const1[0] = 1.0f / static_cast<float>(source_width);
    ec.Const1[1] = 1.0f / static_cast<float>(source_height);
    ec.Const1[2] = static_cast<float>(source_width);
    ec.Const1[3] = static_cast<float>(source_height);

    ec.Const2[0] = static_cast<float>(target_width);
    ec.Const2[1] = static_cast<float>(target_height);
    ec.Const2[2] = 1.0f / static_cast<float>(target_width);
    ec.Const2[3] = 1.0f / static_cast<float>(target_height);

    ctx->UpdateSubresource(g_easu_cb, 0, nullptr, &ec, 0, 0);

    ctx->CSSetShader(g_easu_shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &g_easu_cb);
    ctx->CSSetShaderResources(0, 1, &source_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, &g_inter_uav, nullptr);
    ctx->Dispatch(std::max(1U, (target_width + 15) / 16), std::max(1U, (target_height + 15) / 16), 1);

    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ID3D11UnorderedAccessView* null_uav[1] = { nullptr };
    ctx->CSSetShaderResources(0, 1, null_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

    // 2. Pass 2: RCAS (g_inter_srv -> target_uav)
    FsrRcasConstants rc{};
    rc.RcasConfig[0] = 0.75f; // Sharpness (0.0 to 1.0)
    rc.RcasConfig[1] = static_cast<float>(target_width);
    rc.RcasConfig[2] = static_cast<float>(target_height);
    rc.RcasConfig[3] = 0.0f;

    ctx->UpdateSubresource(g_rcas_cb, 0, nullptr, &rc, 0, 0);

    ctx->CSSetShader(g_rcas_shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &g_rcas_cb);
    ctx->CSSetShaderResources(0, 1, &g_inter_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, &target_uav, nullptr);
    ctx->Dispatch(std::max(1U, (target_width + 15) / 16), std::max(1U, (target_height + 15) / 16), 1);

    ctx->CSSetShaderResources(0, 1, null_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);
}

}  // namespace omnirender::daemon::upscaler
