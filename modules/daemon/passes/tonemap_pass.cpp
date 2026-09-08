// filepath: modules/daemon/passes/tonemap_pass.cpp
// Tonemapping and depth linearization compute passes.

#include "tonemap_pass.h"

#include <algorithm>

namespace omnirender::daemon {

namespace {

struct alignas(16) TonemapConstants {
    float SourceWidth;
    float SourceHeight;
    float TargetWidth;
    float TargetHeight;
    float JitterX;
    float JitterY;
    float BlendFactor;
    float Pad1;
};

}  // namespace

void ExecuteTonemap(ID3D11DeviceContext* ctx,
                    ID3D11ComputeShader* shader,
                    ID3D11Buffer* cb,
                    ID3D11ShaderResourceView* work_srv,
                    ID3D11UnorderedAccessView* work_uav_b,
                    ID3D11Texture2D* work_tex,
                    ID3D11Texture2D* work_tex_b,
                    UINT width, UINT height) {
    if (!ctx || !shader || !cb || !work_srv || !work_uav_b || !work_tex || !work_tex_b) return;

    TonemapConstants tc{};
    tc.SourceWidth  = static_cast<float>(width);
    tc.SourceHeight = static_cast<float>(height);
    tc.TargetWidth  = static_cast<float>(width);
    tc.TargetHeight = static_cast<float>(height);
    ctx->UpdateSubresource(cb, 0, nullptr, &tc, 0, 0);

    ctx->CSSetShader(shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &cb);
    ctx->CSSetShaderResources(0, 1, &work_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, &work_uav_b, nullptr);
    ctx->Dispatch(std::max(1U, (width + 15) / 16), std::max(1U, (height + 15) / 16), 1);

    ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
    ID3D11ShaderResourceView* null_srvs[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
    ctx->CSSetShaderResources(0, 1, null_srvs);

    ctx->CopyResource(work_tex, work_tex_b);
}

void ExecuteLinearizeDepth(ID3D11DeviceContext* ctx,
                           ID3D11ComputeShader* shader,
                           ID3D11Buffer* cb,
                           ID3D11ShaderResourceView* depth_srv,
                           ID3D11UnorderedAccessView* work_uav_b,
                           ID3D11Texture2D* work_tex,
                           ID3D11Texture2D* work_tex_b,
                           UINT width, UINT height) {
    if (!ctx || !shader || !cb || !depth_srv || !work_uav_b || !work_tex || !work_tex_b) return;

    TonemapConstants tc{};
    tc.SourceWidth  = static_cast<float>(width);
    tc.SourceHeight = static_cast<float>(height);
    tc.TargetWidth  = static_cast<float>(width);
    tc.TargetHeight = static_cast<float>(height);
    ctx->UpdateSubresource(cb, 0, nullptr, &tc, 0, 0);

    ctx->CSSetShader(shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &cb);
    ctx->CSSetShaderResources(0, 1, &depth_srv);
    ctx->CSSetUnorderedAccessViews(0, 1, &work_uav_b, nullptr);
    ctx->Dispatch(std::max(1U, (width + 15) / 16), std::max(1U, (height + 15) / 16), 1);

    ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
    ID3D11ShaderResourceView* null_srvs[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
    ctx->CSSetShaderResources(0, 1, null_srvs);

    ctx->CopyResource(work_tex, work_tex_b);
}

}  // namespace omnirender::daemon
