// filepath: modules/daemon/passes/disocclusion_pass.cpp
// Depth-based geometric disocclusion compute pass.

#include "disocclusion_pass.h"

#include <algorithm>

namespace omnirender::daemon {

namespace {

struct alignas(16) DisocclusionShaderConstants {
    float TexelSize[2];
    float DepthThreshold;
    float MotionSensitivity;
    float Width;
    float Height;
    float _Pad0;
    float _Pad1;
};

}  // namespace

int ExecuteDisocclusionMask(ID3D11DeviceContext* ctx,
                            ID3D11ComputeShader* shader,
                            ID3D11Buffer* cb,
                            ID3D11ShaderResourceView* cur_depth_srv,
                            ID3D11ShaderResourceView* prev_depth_srv,
                            ID3D11ShaderResourceView* motion_srv,
                            ID3D11UnorderedAccessView* disocclusion_uav,
                            const OmniRenderIPCFrameData& payload) {
    if (!ctx || !shader || !cb || !cur_depth_srv || !disocclusion_uav) return -1;

    DisocclusionShaderConstants dc{};
    dc.TexelSize[0]      = (payload.surface_width  > 0) ? 1.0f / static_cast<float>(payload.surface_width)  : 0.0f;
    dc.TexelSize[1]      = (payload.surface_height > 0) ? 1.0f / static_cast<float>(payload.surface_height) : 0.0f;
    dc.DepthThreshold    = 0.03f;
    dc.MotionSensitivity = 1.0f;
    dc.Width             = static_cast<float>(payload.surface_width);
    dc.Height            = static_cast<float>(payload.surface_height);
    dc._Pad0             = 0.0f;
    dc._Pad1             = 0.0f;
    ctx->UpdateSubresource(cb, 0, nullptr, &dc, 0, 0);

    ID3D11ShaderResourceView* effective_prev = prev_depth_srv ? prev_depth_srv : cur_depth_srv;
    ID3D11ShaderResourceView* srvs[3] = { cur_depth_srv, effective_prev, motion_srv };
    ctx->CSSetShader(shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &cb);
    ctx->CSSetShaderResources(0, 3, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &disocclusion_uav, nullptr);

    UINT gx = (payload.surface_width  + 15) / 16;
    UINT gy = (payload.surface_height + 15) / 16;
    ctx->Dispatch(std::max(1U, gx), std::max(1U, gy), 1);

    ID3D11UnorderedAccessView* null_uav[1] = { nullptr };
    ID3D11ShaderResourceView* null_srvs[3] = { nullptr, nullptr, nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);
    ctx->CSSetShaderResources(0, 3, null_srvs);
    return 0;
}

}  // namespace omnirender::daemon
