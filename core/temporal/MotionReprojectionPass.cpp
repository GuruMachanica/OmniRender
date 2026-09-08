// filepath: core/temporal/MotionReprojectionPass.cpp
// Depth-reprojected motion vector generation.
// GPU path (optional): MotionReproject.cso dispatch (RG16F UAV), compiled from
//   shaders/temporal/MotionReproject.hlsl.
// CPU path: per-pixel reprojection into staging buffer, uploaded via UploadTextureData.
#include "MotionReprojectionPass.h"
#include <cstdio>
#include <fstream>
#include "ShaderPath.h"
#include "../../graphics/abstraction/IGraphicsDevice.h"
#include "../../graphics/abstraction/ICommandContext.h"
#include "../../graphics/abstraction/IGraphicsTexture.h"
#include "../../graphics/abstraction/IGraphicsBuffer.h"
#include "../../core/resources/TextureDesc.h"
#include "../../core/resources/BufferDesc.h"

namespace omnirender::core::temporal {

static constexpr uint32_t kTileSize = 8u;

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool MotionReprojectionPass::Initialize(graphics::IGraphicsDevice& device,
                                        uint32_t width, uint32_t height) {
    Shutdown();
    device_ = &device;
    width_  = width;
    height_ = height;

    // Allocate RG16F output texture for motion vectors (UAV + SRV).
    TextureDesc mv_desc{ width, height, 1, TextureFormat::R16G16_FLOAT,
        TextureUsage::UnorderedAccess | TextureUsage::ShaderResource |
        TextureUsage::TransferSrc, "MotionVectors" };
    auto tex = device.CreateTexture(mv_desc);
    if (!tex) return false;
    output_motion_ = GpuTexture(std::move(tex));

    // Constant buffer for reprojection matrix.
    BufferDesc cb_desc{ sizeof(ReprojectionCB), BufferUsage::ConstantBuffer, "MotionReprojCB" };
    cb_buffer_ = device.CreateBuffer(cb_desc, nullptr);

    // Attempt to load the pre-compiled compute shader (optional GPU accelerator).
    // The CSO must be compiled from shaders/temporal/MotionReproject.hlsl, whose
    // cbuffer layout matches ReprojectionCB. Do NOT point this at the daemon's
    // modules/shaders/motion_reproject_hlsl.hlsl — that constant-buffer layout
    // belongs to the daemon-side D3D11 pipeline and is incompatible here.
    // ShaderPath resolves relative to the running executable, not CWD. When the
    // CSO is absent the CPU path remains fully functional.
    const std::string cso_path = GetShaderPath("MotionReproject.cso");
    std::ifstream cso(cso_path, std::ios::binary | std::ios::ate);
    if (cso.is_open()) {
        auto sz = static_cast<size_t>(cso.tellg());
        cso.seekg(0);
        std::vector<char> blob(sz);
        cso.read(blob.data(), static_cast<std::streamsize>(sz));
        void* native_dev = device.GetNativeDevice();
        if (native_dev) {
            ID3D11Device* d3d = static_cast<ID3D11Device*>(native_dev);
            ID3D11ComputeShader* cs = nullptr;
            HRESULT hr = d3d->CreateComputeShader(blob.data(), sz, nullptr, &cs);
            if (SUCCEEDED(hr)) gpu_shader_ = cs;
        }
    }
    // Always allocate CPU scratch regardless of GPU path availability so the
    // static-camera zero-clear path and the first-frame fallback always work.
    cpu_pixels_.assign(static_cast<size_t>(width_) * height_ * 2u, uint16_t(0));
    return true;
}

void MotionReprojectionPass::Shutdown() {
    if (gpu_shader_) {
        static_cast<IUnknown*>(gpu_shader_)->Release();
        gpu_shader_ = nullptr;
    }
    output_motion_.Reset();
    cb_buffer_.reset();
    cpu_pixels_.clear();
    device_ = nullptr;
    width_ = height_ = 0;
}

// ---------------------------------------------------------------------------
// Execute dispatch
// ---------------------------------------------------------------------------
PassResult MotionReprojectionPass::Execute(FrameContext& fc,
                                           graphics::ICommandContext& cmd) {
    if (!device_ || !output_motion_.IsValid()) return PassResult::Failed;
    // Require depth for reprojection; skip gracefully if unavailable.
    if (!fc.depth.IsValid()) return PassResult::Skipped;

    // Static camera: zero out the motion texture so DLSS doesn't see stale vectors
    // from a previous frame where the camera was moving.
    if (!fc.camera.HasMovement()) {
        std::fill(cpu_pixels_.begin(), cpu_pixels_.end(), uint16_t(0));
        cmd.UploadTextureData(output_motion_.Get(), cpu_pixels_.data(), width_ * 4u);
        fc.motion = output_motion_;
        fc.validity.motion_valid = true;
        return PassResult::Success;
    }

    PassResult res = gpu_shader_ ? ExecuteGpu(fc, cmd) : ExecuteCpu(fc, cmd);
    if (res == PassResult::Success) {
        fc.motion = output_motion_;
        fc.validity.motion_valid = true;
    }
    return res;
}

// ---------------------------------------------------------------------------
// GPU path
// ---------------------------------------------------------------------------
PassResult MotionReprojectionPass::ExecuteGpu(FrameContext& fc,
                                              graphics::ICommandContext& cmd) {
    if (!cb_buffer_) return PassResult::Failed;
    auto cb = MakeReprojectionCB(fc);
    cmd.UpdateBuffer(cb_buffer_.get(), &cb, sizeof(cb));

    graphics::IGraphicsBuffer* cbs[] = { cb_buffer_.get() };
    cmd.SetConstantBuffers(0, 1, cbs);

    graphics::IGraphicsTexture* srvs[] = { fc.depth.Get() };
    cmd.SetShaderResources(0, 1, srvs);

    graphics::IGraphicsTexture* uavs[] = { output_motion_.Get() };
    cmd.SetUnorderedAccessViews(0, 1, uavs);

    cmd.SetComputeShader(gpu_shader_);
    cmd.Dispatch(GroupCount(width_, kTileSize), GroupCount(height_, kTileSize), 1);

    // Unbind UAV.
    graphics::IGraphicsTexture* null_uav[] = { nullptr };
    cmd.SetUnorderedAccessViews(0, 1, null_uav);
    return PassResult::Success;
}

// ---------------------------------------------------------------------------
// CPU camera-motion approximation fallback
// NOTE: This is NOT per-pixel depth reprojection. It uses a fixed mid-depth
// (ndcZ = 0.5) and therefore produces a camera-motion approximation rather
// than true depth-correct motion vectors. The GPU path produces accurate
// depth-reprojected vectors; the CPU path is only an emergency fallback.
// ---------------------------------------------------------------------------
PassResult MotionReprojectionPass::ExecuteCpu(FrameContext& fc,
                                              graphics::ICommandContext& cmd) {
    auto cb = MakeReprojectionCB(fc);
    const float* pvp = cb.prev_view_proj;
    const float* ivp = cb.inv_view_proj;
    const uint32_t W = width_, H = height_;

    for (uint32_t py = 0; py < H; ++py) {
        for (uint32_t px = 0; px < W; ++px) {
            float ndcX = (static_cast<float>(px) + 0.5f) * cb.rcp_width  * 2.0f - 1.0f;
            float ndcY = 1.0f - (static_cast<float>(py) + 0.5f) * cb.rcp_height * 2.0f;
            float ndcZ = 0.5f; // camera-motion approximation: fixed mid-depth

            float wx, wy, wz, ww;
            if (!TransformPoint(ivp, ndcX, ndcY, ndcZ, 1.0f, wx, wy, wz, ww))
                continue;
            float invW = 1.0f / ww;
            wx *= invW; wy *= invW; wz *= invW;

            float px2, py2, pz2, pw2;
            if (!TransformPoint(pvp, wx, wy, wz, 1.0f, px2, py2, pz2, pw2))
                continue;
            float invPw = 1.0f / pw2;

            // Pack as RG16F (2 x fp16 per pixel).
            size_t idx = (static_cast<size_t>(py) * W + px) * 2;
            cpu_pixels_[idx + 0] = FloatToHalf(ndcX - px2 * invPw);
            cpu_pixels_[idx + 1] = FloatToHalf(ndcY - py2 * invPw);
        }
    }

    // Row pitch for RG16F = width * 2 channels * 2 bytes = width * 4.
    cmd.UploadTextureData(output_motion_.Get(), cpu_pixels_.data(), W * 4u);
    return PassResult::Success;
}

// IEEE 754 binary16 (half-float) conversion.
uint16_t MotionReprojectionPass::FloatToHalf(float f) noexcept {
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    uint16_t sign  = static_cast<uint16_t>((x >> 16) & 0x8000u);
    int32_t  exp   = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant  = x & 0x7FFFFFu;
    if (exp <= 0)  return sign; // underflow -> zero
    if (exp >= 31) return sign | 0x7C00u; // overflow -> infinity
    return sign | static_cast<uint16_t>(exp << 10) | static_cast<uint16_t>(mant >> 13);
}

}  // namespace omnirender::core::temporal
