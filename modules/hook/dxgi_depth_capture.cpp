// filepath: modules/hook/dxgi_depth_capture.cpp
// Depth-buffer tracking and camera-matrix heuristic for the DXGI capture path.
//
// Depth capture strategy:
//   Hook ID3D11DeviceContext::OMSetRenderTargets (vtable index 33).
//   When a DSV is bound that matches the current surface size, record its
//   resource pointer. On Present, copy it to the shared depth texture.
//
// Camera capture strategy:
//   Hook ID3D11DeviceContext::VSSetConstantBuffers (vtable index 43).
//   For each buffer bound to VS slots 0-3, Map it and scan for a 4x4
//   float matrix that passes finite/determinant/near-far checks.
//   The first valid hit is stored as the current view-projection.
//   This is best-effort and game-specific; it produces camera_valid=true
//   when it succeeds so DLSS/motion passes become active.

#include "dxgi_depth_capture.h"

#include <cmath>
#include <cstring>
#include <d3d11.h>

#include "../common/logging.h"
#include "../common/vtable_hook.h"

namespace omnirender::hook {

namespace {

// Vtable indices for ID3D11DeviceContext (D3D11 SDK, immediate context).
constexpr omnirender::VTableIndex kOMSetRenderTargets    = 33;
constexpr omnirender::VTableIndex kVSSetConstantBuffers  = 43;

using PFN_OMSetRenderTargets = void (STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
    ID3D11DepthStencilView*);

using PFN_VSSetConstantBuffers = void (STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);

struct OriginalCtx {
    PFN_OMSetRenderTargets   OMSetRenderTargets  = nullptr;
    PFN_VSSetConstantBuffers VSSetConstantBuffers = nullptr;
};

OriginalCtx g_orig;
bool        g_hooks_installed = false;

// Last-seen depth state.
ID3D11Resource* g_tracked_depth_resource = nullptr;
uint32_t        g_tracked_depth_w        = 0;
uint32_t        g_tracked_depth_h        = 0;

// Last-seen camera matrix (row-major view*proj).
float  g_camera_vp[16]  = {};
float  g_camera_near    = 0.1f;
float  g_camera_far     = 1000.0f;
bool   g_camera_valid   = false;

// ---------------------------------------------------------------------------
// Camera matrix heuristic
// ---------------------------------------------------------------------------
static bool LooksLikeViewProj(const float* m, float* out_near, float* out_far) noexcept {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;

    float maxv = 0.0f;
    for (int i = 0; i < 16; ++i) maxv = maxv < std::abs(m[i]) ? std::abs(m[i]) : maxv;
    if (maxv < 1e-6f) return false;

    // det of 3x3 rotation block must be non-degenerate.
    const float det3 =
        m[0]*(m[5]*m[10] - m[6]*m[9]) -
        m[1]*(m[4]*m[10] - m[6]*m[8]) +
        m[2]*(m[4]*m[9]  - m[5]*m[8]);
    if (std::abs(det3) < 1e-4f) return false;

    // Recover near/far from projection terms (row-major column-projection).
    // m[10] = -far/(far-near),  m[14] = -far*near/(far-near)  (RH standard)
    // or reversed-Z variants. Clamp to plausible range.
    const float a = m[10], b = m[14];
    if (std::abs(a + 1.0f) < 1e-5f) return false;  // degenerate
    const float derived_near = b / (a + 1.0f);
    const float derived_far  = b / (a - 1.0f + 1e-10f);
    const float n = std::abs(derived_near);
    const float f = std::abs(derived_far);
    if (n <= 0.0f || f <= n || n > 100.0f || f < 10.0f || f > 1e6f)
        return false;

    if (out_near) *out_near = n;
    if (out_far)  *out_far  = f;
    return true;
}

static void ScanBufferForCamera(ID3D11DeviceContext* ctx, ID3D11Buffer* buf) noexcept {
    if (!buf) return;
    D3D11_BUFFER_DESC bd{};
    buf->GetDesc(&bd);
    if (bd.ByteWidth < 64 || !(bd.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) {
        // Most constant buffers are GPU-only; we need a staging copy.
        // Skip to avoid stalling the GPU pipeline on unmappable buffers.
        return;
    }

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(buf, 0, D3D11_MAP_READ, 0, &ms))) return;

    const float* data = static_cast<const float*>(ms.pData);
    const uint32_t float_count = bd.ByteWidth / 4;
    for (uint32_t i = 0; i + 16 <= float_count; i += 4) {
        float n = 0.0f, f = 0.0f;
        if (LooksLikeViewProj(data + i, &n, &f)) {
            std::memcpy(g_camera_vp, data + i, 64);
            g_camera_near  = n;
            g_camera_far   = f;
            g_camera_valid = true;
            ctx->Unmap(buf, 0);
            return;
        }
    }
    ctx->Unmap(buf, 0);
}

// ---------------------------------------------------------------------------
// Hooked ID3D11DeviceContext methods
// ---------------------------------------------------------------------------
void STDMETHODCALLTYPE HookedOMSetRenderTargets(
    ID3D11DeviceContext* self, UINT numViews,
    ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
    if (dsv) {
        ID3D11Resource* res = nullptr;
        dsv->GetResource(&res);
        if (res) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
            dsv->GetDesc(&dvd);
            // Accept D32_FLOAT and D24_UNORM_S8_UINT only.
            if (dvd.Format == DXGI_FORMAT_D32_FLOAT ||
                dvd.Format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D),
                                                  reinterpret_cast<void**>(&tex)))) {
                    D3D11_TEXTURE2D_DESC td{};
                    tex->GetDesc(&td);
                    if (g_tracked_depth_resource) g_tracked_depth_resource->Release();
                    g_tracked_depth_resource = res; res->AddRef();
                    g_tracked_depth_w = td.Width;
                    g_tracked_depth_h = td.Height;
                    tex->Release();
                }
            }
            res->Release();
        }
    }
    g_orig.OMSetRenderTargets(self, numViews, rtvs, dsv);
}

void STDMETHODCALLTYPE HookedVSSetConstantBuffers(
    ID3D11DeviceContext* self, UINT start, UINT count, ID3D11Buffer* const* bufs)
{
    // Scan slots 0-3 for a plausible camera matrix only if we don't have one yet.
    if (!g_camera_valid && count > 0 && start <= 3) {
        for (UINT i = 0; i < count && (start + i) <= 3; ++i) {
            ScanBufferForCamera(self, bufs[i]);
            if (g_camera_valid) break;
        }
    }
    g_orig.VSSetConstantBuffers(self, start, count, bufs);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void InstallContextHooks(ID3D11DeviceContext* ctx) noexcept {
    if (g_hooks_installed || !ctx) return;
    void** vtable = *reinterpret_cast<void***>(ctx);
    omnirender::InstallVTableHook(vtable, {
        { kOMSetRenderTargets,   reinterpret_cast<void*>(&HookedOMSetRenderTargets)   },
        { kVSSetConstantBuffers, reinterpret_cast<void*>(&HookedVSSetConstantBuffers) },
    }, &g_orig);
    g_hooks_installed = true;
    OMNI_LOG_INFO("dxgi_depth_capture: context hooks installed");
}

bool CopyTrackedDepth(ID3D11DeviceContext* ctx, ID3D11Texture2D* dst_tex,
                      uint32_t expected_w, uint32_t expected_h) noexcept {
    if (!g_tracked_depth_resource || !dst_tex || !ctx) return false;
    if (g_tracked_depth_w != expected_w || g_tracked_depth_h != expected_h)
        return false;
    ctx->CopyResource(dst_tex, g_tracked_depth_resource);
    return true;
}

bool GetTrackedCamera(float out_view_proj[16],
                      float* out_near, float* out_far) noexcept {
    if (!g_camera_valid) return false;
    std::memcpy(out_view_proj, g_camera_vp, 64);
    if (out_near) *out_near = g_camera_near;
    if (out_far)  *out_far  = g_camera_far;
    return true;
}

void ResetTrackedState() noexcept {
    if (g_tracked_depth_resource) {
        g_tracked_depth_resource->Release();
        g_tracked_depth_resource = nullptr;
    }
    g_tracked_depth_w = g_tracked_depth_h = 0;
    g_camera_valid = false;
}

}  // namespace omnirender::hook
