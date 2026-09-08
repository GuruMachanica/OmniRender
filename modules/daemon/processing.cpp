// filepath: modules/daemon/processing.cpp
// OmniRender frame processing engine and temporal reconstruction core.

#include "processing.h"

#include <dxgi1_4.h>
#include <d3d11.h>
#include <algorithm>
#include <cstdint>

#include "../common/logging.h"
#include "../common/pipeline_config.h"
#include "../common/ipc_protocol.h"
#include "interop_d3d11.h"
#include "presentation_win.h"
#include "shader_loader.h"
#include "history_manager.h"
#include "passes/motion_pass.h"
#include "passes/disocclusion_pass.h"
#include "passes/raytrace_pass.h"
#include "passes/tonemap_pass.h"

namespace omnirender::daemon {

namespace {

constexpr UINT kMaxProcessingWidth  = omnirender::kDefaultMaxProcessingWidth;
constexpr UINT kMaxProcessingHeight = omnirender::kDefaultMaxProcessingHeight;
constexpr UINT kMaxHistoryFrames    = omnirender::kDefaultMaxHistoryFrames;
constexpr UINT kProcessingVRamMB     = omnirender::kDefaultProcessingVRamMB;

struct alignas(16) ReconstructConstants {
    float SourceWidth, SourceHeight, TargetWidth, TargetHeight;
    float JitterX, JitterY, BaseBlend, ClampBoxScale;
};

struct FrameResources {
    ID3D11ShaderResourceView *input_color_srv = nullptr, *input_depth_srv = nullptr, *work_srv = nullptr, *work_srv_b = nullptr;
    ID3D11Texture2D          *work_texture = nullptr, *work_texture_b = nullptr;
    ID3D11UnorderedAccessView *work_uav = nullptr, *work_uav_b = nullptr;

    ID3D11Texture2D          *motion_texture = nullptr, *reactive_texture = nullptr, *disocclusion_texture = nullptr;
    ID3D11UnorderedAccessView *motion_uav = nullptr, *reactive_uav = nullptr, *disocclusion_uav = nullptr;
    ID3D11ShaderResourceView *motion_srv = nullptr, *reactive_srv = nullptr, *disocclusion_srv = nullptr;

    ID3D11Buffer             *reproject_constants = nullptr, *disocclusion_constants = nullptr, *constants_buffer = nullptr, *raytrace_constants = nullptr;
    ID3D11ComputeShader      *upscale_shader = nullptr, *reconstruct_shader = nullptr, *linearize_shader = nullptr, *tonemap_shader = nullptr;
    ID3D11ComputeShader      *motion_reproject_shader = nullptr, *reactive_mask_shader = nullptr, *disocclusion_shader = nullptr, *raytrace_shader = nullptr;
    ID3D11VertexShader       *passthrough_vs = nullptr; ID3D11PixelShader *passthrough_ps = nullptr; ID3D11SamplerState *linear_sampler = nullptr;
};

FrameResources g_processing{};
HistoryManager g_history_mgr{};
int g_debug_mode = 0;

void ChooseWorkingResolution(UINT sw, UINT sh, UINT tw, UINT th, UINT& ow, UINT& oh) {
    ow = std::min((sw > tw) ? tw : sw, kMaxProcessingWidth);
    oh = std::min((sh > th) ? th : sh, kMaxProcessingHeight);
}

HRESULT CreateProcessingBuffers(UINT width, UINT height) {
    ID3D11Device* device = Device();
    if (!device) return E_FAIL;

    IUnknown* old_buffers[] = {
        g_processing.work_texture, g_processing.work_srv, g_processing.work_uav, g_processing.work_texture_b,
        g_processing.work_srv_b, g_processing.work_uav_b, g_processing.motion_texture, g_processing.motion_uav,
        g_processing.motion_srv, g_processing.reactive_texture, g_processing.reactive_uav, g_processing.reactive_srv,
        g_processing.disocclusion_texture, g_processing.disocclusion_uav, g_processing.disocclusion_srv,
        g_processing.reproject_constants, g_processing.disocclusion_constants
    };
    for (auto* o : old_buffers) if (o) o->Release();

    auto make_tex = [device](UINT w, UINT h, DXGI_FORMAT fmt, UINT bind, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv, ID3D11UnorderedAccessView** uav) {
        D3D11_TEXTURE2D_DESC d{ w, h, 1, 1, fmt, {1, 0}, D3D11_USAGE_DEFAULT, bind, 0, 0 };
        if (SUCCEEDED(device->CreateTexture2D(&d, nullptr, tex))) {
            if (srv && (bind & D3D11_BIND_SHADER_RESOURCE)) device->CreateShaderResourceView(*tex, nullptr, srv);
            if (uav && (bind & D3D11_BIND_UNORDERED_ACCESS)) device->CreateUnorderedAccessView(*tex, nullptr, uav);
        }
    };
    constexpr UINT kBindSRV_UAV = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    make_tex(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, kBindSRV_UAV, &g_processing.work_texture, &g_processing.work_srv, &g_processing.work_uav);
    make_tex(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, kBindSRV_UAV, &g_processing.work_texture_b, &g_processing.work_srv_b, &g_processing.work_uav_b);
    make_tex(width, height, DXGI_FORMAT_R16G16_FLOAT, kBindSRV_UAV, &g_processing.motion_texture, &g_processing.motion_srv, &g_processing.motion_uav);
    make_tex(width, height, DXGI_FORMAT_R8_UNORM, kBindSRV_UAV, &g_processing.reactive_texture, &g_processing.reactive_srv, &g_processing.reactive_uav);
    make_tex(width, height, DXGI_FORMAT_R8_UNORM, kBindSRV_UAV, &g_processing.disocclusion_texture, &g_processing.disocclusion_srv, &g_processing.disocclusion_uav);

    auto make_cb = [device](UINT bytes, ID3D11Buffer** out) {
        if (*out) { (*out)->Release(); *out = nullptr; }
        D3D11_BUFFER_DESC b{ bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
        device->CreateBuffer(&b, nullptr, out);
    };
    make_cb(sizeof(ReconstructConstants), &g_processing.constants_buffer);
    make_cb(256, &g_processing.reproject_constants);
    make_cb(256, &g_processing.disocclusion_constants);

    return g_history_mgr.Initialize(device, width, height);
}

void DownscaleToWork(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* source_srv, UINT sw, UINT sh, UINT ww, UINT wh) {
    if (!ctx || !source_srv || !g_processing.work_uav || !g_processing.upscale_shader) return;
    ReconstructConstants cb{ static_cast<float>(sw), static_cast<float>(sh), static_cast<float>(ww), static_cast<float>(wh), 0, 0, 0, 0 };
    ctx->UpdateSubresource(g_processing.constants_buffer, 0, nullptr, &cb, 0, 0);
    ctx->CSSetShader(g_processing.upscale_shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &g_processing.constants_buffer);
    ctx->CSSetShaderResources(0, 1, &source_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, &g_processing.work_uav, nullptr);
    ctx->Dispatch(std::max(1U, (ww + 15) / 16), std::max(1U, (wh + 15) / 16), 1);
    ID3D11ShaderResourceView* null_srv[1] = { nullptr }; ID3D11UnorderedAccessView* null_uav[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr); ctx->CSSetShaderResources(0, 1, null_srv);
}

void ResolveFrame(ID3D11DeviceContext* ctx, UINT width, UINT height, bool enable_reconstruction) {
    if (!ctx || !g_processing.work_uav_b || !g_processing.work_srv || !enable_reconstruction || !g_processing.reconstruct_shader) return;
    ID3D11ShaderResourceView* prev_srv = g_history_mgr.GetCurrentHistorySrv();
    float blend = (prev_srv && g_history_mgr.HasValidHistory()) ? 0.88f : 0.0f;

    ReconstructConstants cb{ static_cast<float>(width), static_cast<float>(height), static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f, blend, 1.25f };
    ctx->UpdateSubresource(g_processing.constants_buffer, 0, nullptr, &cb, 0, 0);
    ctx->CSSetShader(g_processing.reconstruct_shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &g_processing.constants_buffer);
    if (g_processing.linear_sampler) ctx->CSSetSamplers(0, 1, &g_processing.linear_sampler);
    ID3D11ShaderResourceView* srvs[5] = { g_processing.work_srv, prev_srv ? prev_srv : g_processing.work_srv, g_processing.motion_srv, g_processing.disocclusion_srv, g_processing.reactive_srv };
    ctx->CSSetShaderResources(0, 5, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &g_processing.work_uav_b, nullptr);
    ctx->Dispatch(std::max(1U, (width + 15) / 16), std::max(1U, (height + 15) / 16), 1);
    ID3D11UnorderedAccessView* null_uavs[1] = { nullptr }; ID3D11ShaderResourceView* null_srvs[5] = {};
    ctx->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr); ctx->CSSetShaderResources(0, 5, null_srvs);
    ctx->CopyResource(g_processing.work_texture, g_processing.work_texture_b);
}

} // namespace

int InitializeProcessing() {
    ID3D11Device* device = Device();
    if (!device) return -1;
    const std::pair<const wchar_t*, ID3D11ComputeShader**> shaders[] = {
        { L"modules/shaders/upscale_hlsl.cso", &g_processing.upscale_shader },
        { L"modules/shaders/reconstruct_hlsl.cso", &g_processing.reconstruct_shader },
        { L"modules/shaders/depth_linearize_hlsl.cso", &g_processing.linearize_shader },
        { L"modules/shaders/auto_hdr_tonemap.cso", &g_processing.tonemap_shader },
        { L"modules/shaders/motion_reproject_hlsl.cso", &g_processing.motion_reproject_shader },
        { L"modules/shaders/reactive_mask_hlsl.cso", &g_processing.reactive_mask_shader },
        { L"modules/shaders/disocclusion_hlsl.cso", &g_processing.disocclusion_shader },
        { L"modules/shaders/raytrace_screen_space.cso", &g_processing.raytrace_shader },
    };
    for (auto& [path, s] : shaders) LoadComputeShader(device, path, s);
    EnsurePassthroughShaders(device, &g_processing.passthrough_vs, &g_processing.passthrough_ps);
    if (!g_processing.linear_sampler) {
        D3D11_SAMPLER_DESC samp{ D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP };
        device->CreateSamplerState(&samp, &g_processing.linear_sampler);
    }
    if (!g_processing.raytrace_constants) {
        D3D11_BUFFER_DESC b{ 256, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
        device->CreateBuffer(&b, nullptr, &g_processing.raytrace_constants);
    }
    return 0;
}

void ShutdownProcessing() {
    IUnknown* objs[] = {
        g_processing.constants_buffer, g_processing.upscale_shader, g_processing.reconstruct_shader, g_processing.linearize_shader,
        g_processing.tonemap_shader, g_processing.motion_reproject_shader, g_processing.reactive_mask_shader, g_processing.disocclusion_shader,
        g_processing.raytrace_shader, g_processing.raytrace_constants, g_processing.passthrough_vs, g_processing.passthrough_ps,
        g_processing.linear_sampler, g_processing.motion_uav, g_processing.motion_srv, g_processing.motion_texture,
        g_processing.reactive_uav, g_processing.reactive_srv, g_processing.reactive_texture, g_processing.disocclusion_uav,
        g_processing.disocclusion_srv, g_processing.disocclusion_texture,
        g_processing.reproject_constants, g_processing.disocclusion_constants, g_processing.work_uav, g_processing.work_srv,
        g_processing.work_texture, g_processing.work_uav_b, g_processing.work_srv_b, g_processing.work_texture_b,
        g_processing.input_color_srv, g_processing.input_depth_srv
    };
    for (auto* obj : objs) if (obj) obj->Release();
    g_history_mgr.Shutdown();
    memset(&g_processing, 0, sizeof(g_processing));
}

int PrepareFrame(OmniRenderIPCFrameData const& payload, ID3D11Texture2D* color, ID3D11Texture2D* depth, bool, bool) {
    ID3D11Device* device = Device(); ID3D11DeviceContext* ctx = Context();
    if (!device || !ctx) return -1;
    if (g_processing.input_color_srv) { g_processing.input_color_srv->Release(); g_processing.input_color_srv = nullptr; }
    if (g_processing.input_depth_srv) { g_processing.input_depth_srv->Release(); g_processing.input_depth_srv = nullptr; }
    if (color) device->CreateShaderResourceView(color, nullptr, &g_processing.input_color_srv);
    if (depth) device->CreateShaderResourceView(depth, nullptr, &g_processing.input_depth_srv);

    UINT ww = 0, wh = 0;
    ChooseWorkingResolution(payload.surface_width, payload.surface_height, payload.target_width, payload.target_height, ww, wh);
    if (!g_processing.work_texture && FAILED(CreateProcessingBuffers(ww, wh))) return -2;
    D3D11_TEXTURE2D_DESC d{}; g_processing.work_texture->GetDesc(&d);
    if ((d.Width != ww || d.Height != wh) && FAILED(CreateProcessingBuffers(ww, wh))) return -2;
    if (g_processing.upscale_shader && g_processing.input_color_srv) {
        DownscaleToWork(ctx, g_processing.input_color_srv, payload.surface_width, payload.surface_height, ww, wh);
    } else if (color && g_processing.work_texture && payload.surface_width == ww && payload.surface_height == wh) {
        ctx->CopyResource(g_processing.work_texture, color);
    }
    return 0;
}

int ResolveTemporal(ID3D11DeviceContext* ctx, UINT width, UINT height, bool enable_reconstruction) {
    if (!ctx) return -1;
    if ((width == 0 || height == 0) && g_processing.work_texture) {
        D3D11_TEXTURE2D_DESC d{}; g_processing.work_texture->GetDesc(&d); width = d.Width; height = d.Height;
    }
    ResolveFrame(ctx, width, height, enable_reconstruction);
    return 0;
}

int BuildMotionVectors(OmniRenderIPCFrameData const& payload, ID3D11Texture2D* depth, ID3D11Texture2D*) {
    if (!depth || !g_processing.motion_reproject_shader) return -1;
    if (!g_processing.input_depth_srv && Device()) Device()->CreateShaderResourceView(depth, nullptr, &g_processing.input_depth_srv);
    return ExecuteMotionReproject(Context(), g_processing.motion_reproject_shader, g_processing.reproject_constants,
                                  g_processing.input_depth_srv, g_history_mgr.GetPreviousDepthSrv(), g_processing.motion_uav, payload);
}

int BuildReactiveMask(OmniRenderIPCFrameData const& payload, ID3D11Texture2D* cur, ID3D11Texture2D* prev, ID3D11Texture2D* depth, ID3D11Texture2D*) {
    if (!cur || !prev || !depth || !g_processing.reactive_mask_shader) return -1;
    ID3D11Device* dev = Device(); if (!dev) return -1;
    ID3D11ShaderResourceView *c_srv = nullptr, *p_srv = nullptr, *d_srv = nullptr;
    dev->CreateShaderResourceView(cur, nullptr, &c_srv); dev->CreateShaderResourceView(prev, nullptr, &p_srv); dev->CreateShaderResourceView(depth, nullptr, &d_srv);
    int rc = ExecuteReactiveMask(Context(), g_processing.reactive_mask_shader, g_processing.reproject_constants, c_srv, p_srv, d_srv, g_processing.reactive_uav, payload);
    if (c_srv) c_srv->Release(); if (p_srv) p_srv->Release(); if (d_srv) d_srv->Release();
    return rc;
}

int BuildDisocclusionMask(OmniRenderIPCFrameData const& payload, ID3D11Texture2D* depth, ID3D11Texture2D*, ID3D11Texture2D*, ID3D11Texture2D*) {
    if (!depth || !g_processing.disocclusion_shader) return -1;
    return ExecuteDisocclusionMask(Context(), g_processing.disocclusion_shader, g_processing.disocclusion_constants,
                                  g_processing.input_depth_srv, g_history_mgr.GetPreviousDepthSrv(), g_processing.motion_srv,
                                  g_processing.disocclusion_uav, payload);
}

void DispatchTonemap(ID3D11DeviceContext* ctx, UINT w, UINT h) {
    if (ctx && omnirender::config::g_enable_tonemap)
        ExecuteTonemap(ctx, g_processing.tonemap_shader ? g_processing.tonemap_shader : g_processing.linearize_shader, g_processing.constants_buffer, g_processing.work_srv, g_processing.work_uav_b, g_processing.work_texture, g_processing.work_texture_b, w, h);
}

void DispatchRayTracing(ID3D11DeviceContext* ctx, UINT w, UINT h) {
    if (ctx && omnirender::config::g_enable_rt_effects)
        ExecuteRayTracing(ctx, g_processing.raytrace_shader, g_processing.raytrace_constants, g_processing.work_srv, g_processing.input_depth_srv, g_processing.work_uav_b, g_processing.work_texture, g_processing.work_texture_b, w, h);
}

void PresentProcessed(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, UINT target_w, UINT target_h, bool) {
    if (!ctx || !srv) return;
    ID3D11RenderTargetView* rtv = RenderTargetView(); if (!rtv) return;
    D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(target_w ? target_w : 1), static_cast<float>(target_h ? target_h : 1), 0.0f, 1.0f };
    ctx->OMSetRenderTargets(1, &rtv, nullptr); ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_processing.passthrough_vs, nullptr, 0); ctx->PSSetShader(g_processing.passthrough_ps, nullptr, 0);
    if (g_processing.linear_sampler) ctx->PSSetSamplers(0, 1, &g_processing.linear_sampler);
    ctx->PSSetShaderResources(0, 1, &srv); ctx->Draw(3, 0);
    ID3D11ShaderResourceView* null_srv[1] = {}; ctx->PSSetShaderResources(0, 1, null_srv);
}

void EndFrameProcessing(ID3D11DeviceContext* ctx, ID3D11Texture2D* current_depth) {
    if (ctx && g_processing.work_texture) {
        g_history_mgr.CommitFrame(ctx, g_processing.work_texture, current_depth);
    }
}

void InvalidateTemporalHistory() noexcept {
    g_history_mgr.Invalidate(InvalidationReason::Generic);
}

bool ProcessingCapabilities() { return g_processing.work_texture != nullptr; }
ID3D11ShaderResourceView* ProcessingWorkSrv() noexcept { return g_processing.work_srv; }
ID3D11ShaderResourceView* ProcessingHistorySrv() noexcept { return g_history_mgr.GetCurrentHistorySrv(); }

int CycleDebugMode() noexcept {
    g_debug_mode = (g_debug_mode + 1) % 7;
    const char* names[] = { "Final Output", "Raw Color", "Linearized Depth", "Motion Vectors", "Reactive Mask", "Disocclusion Mask", "History Buffer" };
    OMNI_LOG_INFO("Frame Debugger: Switched to channel %d (%s)", g_debug_mode, names[g_debug_mode]);
    return g_debug_mode;
}

int GetDebugMode() noexcept { return g_debug_mode; }

ID3D11ShaderResourceView* GetDebugChannelSrv(int ch) noexcept {
    ID3D11ShaderResourceView* ch_map[] = { g_processing.work_srv, g_processing.input_color_srv, g_processing.input_depth_srv,
                                           g_processing.motion_srv, g_processing.reactive_srv, g_processing.disocclusion_srv,
                                           g_history_mgr.GetCurrentHistorySrv() };
    return (ch >= 1 && ch <= 6 && ch_map[ch]) ? ch_map[ch] : g_processing.work_srv;
}

}  // namespace omnirender::daemon
