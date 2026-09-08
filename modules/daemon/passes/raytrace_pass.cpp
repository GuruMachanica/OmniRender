// filepath: modules/daemon/passes/raytrace_pass.cpp
// Screen-Space Ray Traced Reflections and Ambient Occlusion.

#include "raytrace_pass.h"

#include <algorithm>
#include <cmath>

namespace omnirender::daemon {

namespace {

struct alignas(16) RaytraceConstants {
    float ViewParams[4];  // Near, Far, Width, Height
    float RayParams[4];   // MaxDist, StepSize, Steps, Intensity
    float InvProj[16];
    float Proj[16];
};

}  // namespace

void ExecuteRayTracing(ID3D11DeviceContext* ctx,
                       ID3D11ComputeShader* shader,
                       ID3D11Buffer* cb,
                       ID3D11ShaderResourceView* work_srv,
                       ID3D11ShaderResourceView* depth_srv,
                       ID3D11UnorderedAccessView* work_uav_b,
                       ID3D11Texture2D* work_tex,
                       ID3D11Texture2D* work_tex_b,
                       UINT width, UINT height) {
    if (!ctx || !shader || !work_srv || !work_uav_b || !depth_srv || !work_tex || !work_tex_b) return;

    RaytraceConstants rc{};
    rc.ViewParams[0] = 0.1f;
    rc.ViewParams[1] = 1000.0f;
    rc.ViewParams[2] = static_cast<float>(width);
    rc.ViewParams[3] = static_cast<float>(height);

    rc.RayParams[0] = 40.0f;  // MaxDistance
    rc.RayParams[1] = 0.35f;  // StepSize
    rc.RayParams[2] = 24.0f;  // MaxSteps
    rc.RayParams[3] = 0.40f;  // Reflection intensity

    float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.777f;
    float fov = 1.04719755f;  // 60 deg
    float tanHalfFov = tanf(fov * 0.5f);
    rc.Proj[0]  = 1.0f / (aspect * tanHalfFov);
    rc.Proj[5]  = 1.0f / tanHalfFov;
    rc.Proj[10] = 1000.0f / (1000.0f - 0.1f);
    rc.Proj[11] = 1.0f;
    rc.Proj[14] = -(1000.0f * 0.1f) / (1000.0f - 0.1f);

    rc.InvProj[0]  = aspect * tanHalfFov;
    rc.InvProj[5]  = tanHalfFov;
    rc.InvProj[11] = (1000.0f - 0.1f) / -(1000.0f * 0.1f);
    rc.InvProj[14] = 1.0f;
    rc.InvProj[15] = 1000.0f / (1000.0f * 0.1f);

    if (cb) {
        ctx->UpdateSubresource(cb, 0, nullptr, &rc, 0, 0);
    }

    ID3D11ShaderResourceView* srvs[2] = { work_srv, depth_srv };
    ctx->CSSetShader(shader, nullptr, 0);
    if (cb) {
        ctx->CSSetConstantBuffers(0, 1, &cb);
    }
    ctx->CSSetShaderResources(0, 2, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &work_uav_b, nullptr);
    ctx->Dispatch(std::max(1U, (width + 15) / 16), std::max(1U, (height + 15) / 16), 1);

    ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
    ID3D11ShaderResourceView* null_srvs[2] = { nullptr, nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
    ctx->CSSetShaderResources(0, 2, null_srvs);

    ctx->CopyResource(work_tex, work_tex_b);
}

}  // namespace omnirender::daemon
