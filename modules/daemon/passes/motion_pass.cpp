// filepath: modules/daemon/passes/motion_pass.cpp
// Motion vector and reactive mask compute passes.

#include "motion_pass.h"

#include <algorithm>

namespace omnirender::daemon {

namespace {

struct alignas(16) ReprojectConstants {
    float ViewProjCurrent[16];
    float ViewProjPrevious[16];
    float InvViewProjCurrent[16];
    float TexelSize[2];
    float CameraNear;
    float CameraFar;
    uint32_t ReversedZ;
    uint32_t MotionValid;
    uint32_t _Pad0;
    uint32_t _Pad1;
};

struct alignas(16) ReactiveConstants {
    float TexelSize[2];
    float ColorThreshold;
    float DepthThreshold;
    float Scale;
    float _Pad0;
    float _Pad1;
    float _Pad2;
};

bool Invert4x4(const float m[16], float out[16]) {
    auto M = [&](int r, int c) -> float { return m[r * 4 + c]; };
    auto SET = [&](int r, int c, float val) { out[r * 4 + c] = val; };

    float n11 = M(0,0), n12 = M(1,0), n13 = M(2,0), n14 = M(3,0);
    float n21 = M(0,1), n22 = M(1,1), n23 = M(2,1), n24 = M(3,1);
    float n31 = M(0,2), n32 = M(1,2), n33 = M(2,2), n34 = M(3,2);
    float n41 = M(0,3), n42 = M(1,3), n43 = M(2,3), n44 = M(3,3);

    float t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
    float t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
    float t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
    float t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;

    float det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
    if (std::abs(det) < 1e-8f) {
        for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return false;
    }
    float idet = 1.0f / det;

    SET(0,0, t11 * idet);
    SET(0,1, (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet);
    SET(0,2, (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet);
    SET(0,3, (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet);

    SET(1,0, t12 * idet);
    SET(1,1, (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet);
    SET(1,2, (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet);
    SET(1,3, (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet);

    SET(2,0, t13 * idet);
    SET(2,1, (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet);
    SET(2,2, (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet);
    SET(2,3, (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet);

    SET(3,0, t14 * idet);
    SET(3,1, (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet);
    SET(3,2, (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet);
    SET(3,3, (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet);
    return true;
}

}  // namespace

int ExecuteMotionReproject(ID3D11DeviceContext* ctx,
                           ID3D11ComputeShader* shader,
                           ID3D11Buffer* cb,
                           ID3D11ShaderResourceView* depth_srv,
                           ID3D11ShaderResourceView* prev_depth_srv,
                           ID3D11UnorderedAccessView* motion_uav,
                           const OmniRenderIPCFrameData& payload,
                           bool* out_motion_valid) {
    if (!ctx || !shader || !cb || !depth_srv || !motion_uav) return -1;

    ReprojectConstants rc{};
    bool camera_valid = false;
    for (int i = 0; i < 16; ++i) {
        rc.ViewProjCurrent[i]  = payload.view_proj_current[i];
        rc.ViewProjPrevious[i] = payload.view_proj_previous[i];
        if (std::abs(payload.view_proj_current[i]) > 1e-6f) camera_valid = true;
    }
    bool inv_ok = false;
    if (camera_valid) {
        inv_ok = Invert4x4(rc.ViewProjCurrent, rc.InvViewProjCurrent);
    } else {
        for (int i = 0; i < 16; ++i) rc.InvViewProjCurrent[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    bool motion_valid = camera_valid && inv_ok;
    rc.MotionValid = motion_valid ? 1u : 0u;
    if (out_motion_valid) *out_motion_valid = motion_valid;

    rc.TexelSize[0] = (payload.surface_width  > 0) ? 1.0f / static_cast<float>(payload.surface_width)  : 0.0f;
    rc.TexelSize[1] = (payload.surface_height > 0) ? 1.0f / static_cast<float>(payload.surface_height) : 0.0f;
    rc.CameraNear   = payload.camera_near;
    rc.CameraFar    = payload.camera_far;
    rc.ReversedZ    = (payload.flags & static_cast<uint32_t>(omnirender::IpcFlag::ReversedZ)) ? 1u : 0u;
    ctx->UpdateSubresource(cb, 0, nullptr, &rc, 0, 0);

    ID3D11ShaderResourceView* srvs[2] = { depth_srv, prev_depth_srv ? prev_depth_srv : depth_srv };
    ctx->CSSetShader(shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &cb);
    ctx->CSSetShaderResources(0, 2, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &motion_uav, nullptr);
    UINT gx = (payload.surface_width  + 15) / 16;
    UINT gy = (payload.surface_height + 15) / 16;
    ctx->Dispatch(std::max(1U, gx), std::max(1U, gy), 1);

    ID3D11UnorderedAccessView* null_uav[1] = { nullptr };
    ID3D11ShaderResourceView* null_srv[2] = { nullptr, nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);
    ctx->CSSetShaderResources(0, 2, null_srv);
    return 0;
}

int ExecuteReactiveMask(ID3D11DeviceContext* ctx,
                        ID3D11ComputeShader* shader,
                        ID3D11Buffer* cb,
                        ID3D11ShaderResourceView* cur_srv,
                        ID3D11ShaderResourceView* prev_srv,
                        ID3D11ShaderResourceView* depth_srv,
                        ID3D11UnorderedAccessView* reactive_uav,
                        const OmniRenderIPCFrameData& payload) {
    if (!ctx || !shader || !cb || !cur_srv || !prev_srv || !depth_srv || !reactive_uav) return -1;

    ReactiveConstants rc{};
    rc.TexelSize[0]    = (payload.surface_width  > 0) ? 1.0f / static_cast<float>(payload.surface_width)  : 0.0f;
    rc.TexelSize[1]    = (payload.surface_height > 0) ? 1.0f / static_cast<float>(payload.surface_height) : 0.0f;
    rc.ColorThreshold  = 0.20f;
    rc.DepthThreshold  = 0.10f;
    rc.Scale           = 1.0f;
    ctx->UpdateSubresource(cb, 0, nullptr, &rc, 0, 0);

    ID3D11ShaderResourceView* srvs[3] = { cur_srv, prev_srv, depth_srv };
    ctx->CSSetShader(shader, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &cb);
    ctx->CSSetShaderResources(0, 3, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &reactive_uav, nullptr);
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
