// filepath: core/temporal/OpticalFlowPass.cpp
// Optical flow fallback motion pass. GPU path: OpticalFlow.cso block-search
// dispatch against the committed history color. CPU fallback: same search on
// CPU luma planes (used on WARP/mock contexts, and when the CSO is absent).
#include "OpticalFlowPass.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#if defined(_WIN32)
#include <d3d11.h>  // ID3D11Device / ID3D11ComputeShader (optional GPU accelerator)
#endif
#include "ShaderPath.h"
#include "../../graphics/abstraction/IGraphicsDevice.h"
#include "../../graphics/abstraction/ICommandContext.h"
#include "../../graphics/abstraction/IGraphicsTexture.h"
#include "../../graphics/abstraction/IGraphicsBuffer.h"
#include "../../core/resources/TextureDesc.h"
#include "../../core/resources/BufferDesc.h"

namespace omnirender::core::temporal {

static constexpr uint32_t kTileSize = 8u;

// IEEE 754 binary16 conversion (same encoding the MotionReprojectionPass
// CPU path uses for its RG16F uploads).
uint16_t OpticalFlowPass::FloatToHalf(float f) noexcept {
    const uint32_t bits = std::bit_cast<uint32_t>(f);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t  exp  = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t man  = bits & 0x007FFFFFu;

    if (((bits >> 23) & 0xFFu) == 0xFFu) {  // inf / NaN
        return static_cast<uint16_t>(sign | 0x7C00u | (man ? 0x200u : 0));
    }
    if (exp >= 0x1F) return static_cast<uint16_t>(sign | 0x7C00u);       // overflow -> inf
    if (exp <= 0) {                                                       // subnormal / zero
        if (exp < -10) return static_cast<uint16_t>(sign);
        man |= 0x00800000u;
        const uint32_t shift = static_cast<uint32_t>(1 - exp);
        return static_cast<uint16_t>(sign | (man >> shift));
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (man >> 13));
}

bool OpticalFlowPass::Initialize(graphics::IGraphicsDevice& device,
                                 uint32_t width, uint32_t height) {
    Shutdown();
    device_ = &device;
    width_  = width;
    height_ = height;

    TextureDesc desc{ width, height, 1, TextureFormat::R16G16_FLOAT,
        TextureUsage::ShaderResource | TextureUsage::UnorderedAccess |
        TextureUsage::TransferSrc | TextureUsage::TransferDst,
        "OpticalFlowMotion" };
    auto tex = device.CreateTexture(desc);
    if (!tex) return false;
    output_motion_ = GpuTexture(std::move(tex));

    BufferDesc cb_desc{ sizeof(ReprojectionCB), 0, BufferUsage::ConstantBuffer, "OpticalFlowCB" };
    cb_buffer_ = device.CreateBuffer(cb_desc, nullptr);

    // Optional GPU accelerator from shaders/temporal/OpticalFlow.hlsl.
    const std::string cso_path = GetShaderPath("OpticalFlow.cso");
    std::ifstream cso(cso_path, std::ios::binary | std::ios::ate);
    if (cso.is_open()) {
        auto sz = static_cast<size_t>(cso.tellg());
        cso.seekg(0);
        std::vector<char> blob(sz);
        cso.read(blob.data(), static_cast<std::streamsize>(sz));
#if defined(_WIN32)
        void* native_dev = device.GetNativeDevice();
        if (native_dev) {
            ID3D11Device* d3d = static_cast<ID3D11Device*>(native_dev);
            ID3D11ComputeShader* cs = nullptr;
            if (SUCCEEDED(d3d->CreateComputeShader(blob.data(), sz, nullptr, &cs)))
                gpu_shader_ = cs;
        }
#endif
    }
    // Bounded CPU-path scratch: two input-res RGBA8 planes.
    cpu_curr_.assign(static_cast<size_t>(width_) * height_ * 4u, 0u);
    cpu_prev_.assign(static_cast<size_t>(width_) * height_ * 4u, 0u);
    return true;
}

void OpticalFlowPass::Shutdown() {
    if (gpu_shader_) {
#if defined(_WIN32)
        static_cast<IUnknown*>(gpu_shader_)->Release();
#endif
        gpu_shader_ = nullptr;
    }
    output_motion_.Reset();
    cb_buffer_.reset();
    cpu_curr_.clear();
    cpu_prev_.clear();
    device_ = nullptr;
    width_ = height_ = 0;
}

PassResult OpticalFlowPass::Execute(FrameContext& fc, graphics::ICommandContext& cmd,
                                    const GpuTexture& previous_color) {
    // Fallback contract (audit #20): never overwrite reprojection output.
    if (fc.validity.motion_valid) return PassResult::Skipped;
    if (!device_ || !output_motion_.IsValid()) return PassResult::Failed;
    if (!fc.color.IsValid() || !previous_color.IsValid()) return PassResult::Skipped;

    // Resolution-domain guard (audit plan Commit 5) for the CURRENT frame.
    // The history texture may legitimately live at OUTPUT resolution (the
    // pipeline commits the upscaled frame); both paths map it through scaled
    // coordinates instead of skipping.
    if (fc.color.GetWidth() != width_ || fc.color.GetHeight() != height_) {
        return PassResult::Skipped;
    }

    return gpu_shader_ ? ExecuteGpu(fc, cmd, previous_color)
                       : ExecuteCpu(fc, cmd, previous_color);
}

PassResult OpticalFlowPass::ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd,
                                       const GpuTexture& previous_color) {
    if (!cb_buffer_) return PassResult::Failed;
    ReprojectionCB cb = MakeReprojectionCB(fc);
    // The optical flow shader does not use near/far; those CB slots carry
    // the HISTORY texture dimensions instead so the shader can map
    // history-res (output) lookups into input-res motion coordinates.
    // Documented in both OpticalFlow.hlsl and here; kept in one struct so
    // the shared ReprojectionCB layout stays intact.
    cb.near_z = static_cast<float>(previous_color.GetWidth());
    cb.far_z  = static_cast<float>(previous_color.GetHeight());
    cmd.UpdateBuffer(cb_buffer_.get(), &cb, sizeof(cb));

    graphics::IGraphicsBuffer* cbs[] = { cb_buffer_.get() };
    cmd.SetConstantBuffers(0, 1, cbs);

    graphics::IGraphicsTexture* srvs[] = { fc.color.Get(), previous_color.Get() };
    cmd.SetShaderResources(0, 2, srvs);

    graphics::IGraphicsTexture* uavs[] = { output_motion_.Get() };
    cmd.SetUnorderedAccessViews(0, 1, uavs);

    cmd.SetComputeShader(gpu_shader_);
    cmd.Dispatch(GroupCount(width_, kTileSize), GroupCount(height_, kTileSize), 1);

    graphics::IGraphicsTexture* null_uav[] = { nullptr };
    cmd.SetUnorderedAccessViews(0, 1, null_uav);

    fc.motion = output_motion_;
    fc.validity.motion_valid = true;
    return PassResult::Success;
}

PassResult OpticalFlowPass::ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd,
                                       const GpuTexture& previous_color) {
    const size_t plane = static_cast<size_t>(width_) * height_ * 4u;
    if (cpu_curr_.size() != plane) cpu_curr_.assign(plane, 0u);
    // History may live at OUTPUT resolution; size its readback to the
    // texture's actual dimensions (a fixed input-res buffer would overflow).
    const uint32_t HW = previous_color.GetWidth();
    const uint32_t HH = previous_color.GetHeight();
    const size_t hist_plane = static_cast<size_t>(HW) * HH * 4u;
    if (cpu_prev_.size() != hist_plane) cpu_prev_.assign(hist_plane, 0u);

    if (!cmd.ReadbackTexture(fc.color.Get(), cpu_curr_.data(), cpu_curr_.size())) {
        return PassResult::Skipped;  // readback unavailable
    }
    if (!cmd.ReadbackTexture(previous_color.Get(), cpu_prev_.data(), cpu_prev_.size())) {
        return PassResult::Skipped;
    }

    const uint32_t W = width_, H = height_;
    const int radius = static_cast<int>(search_radius);
    // Readback bytes are RGBA8; normalize luma to [0,1] so the early-out
    // threshold matches the GPU path's UNORM sampling exactly.
    const uint8_t* curr_px = cpu_curr_.data();
    const uint8_t* prev_px = cpu_prev_.data();
    auto luma = [curr_px](size_t idx) {
        return (0.2126f * curr_px[idx] + 0.7152f * curr_px[idx + 1] +
                0.0722f * curr_px[idx + 2]) / 255.0f;
    };
    // History luma through the same per-axis input->history mapping the
    // GPU shader uses (nearest texel).
    auto luma_prev_hist = [prev_px, W, H, HW, HH](uint32_t x, uint32_t y) {
        const uint32_t hx = (HW > 0) ? std::min(x * HW / W, HW - 1u) : 0;
        const uint32_t hy = (HH > 0) ? std::min(y * HH / H, HH - 1u) : 0;
        const size_t idx = (static_cast<size_t>(hy) * HW + hx) * 4u;
        return (0.2126f * prev_px[idx] + 0.7152f * prev_px[idx + 1] +
                0.0722f * prev_px[idx + 2]) / 255.0f;
    };

    // RG16F output scratch (2 x uint16_t per pixel).
    std::vector<uint16_t> motion(static_cast<size_t>(W) * H * 2u, 0u);

    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const size_t idx = (static_cast<size_t>(y) * W + x) * 4u;
            const float curr = luma(idx);
            const float prev_center = luma_prev_hist(x, y);
            if (std::abs(curr - prev_center) < 0.004f) continue;  // static pixel

            float best_score = 1e9f;
            int best_dx = 0, best_dy = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int sy = static_cast<int>(y) + dy;
                if (sy < 0 || sy >= static_cast<int>(H)) continue;
                for (int dx = -radius; dx <= radius; dx++) {
                    const int sx = static_cast<int>(x) + dx;
                    if (sx < 0 || sx >= static_cast<int>(W)) continue;
                    const float score = std::abs(luma_prev_hist(sx, sy) - curr);
                    if (score < best_score) {
                        best_score = score;
                        best_dx = dx;
                        best_dy = dy;
                    }
                }
            }
            // NDC motion (current - previous), matching MotionReproject.hlsl.
            const float mx = 2.0f * best_dx / static_cast<float>(W);
            const float my = 2.0f * best_dy / static_cast<float>(H);
            const size_t oi = (static_cast<size_t>(y) * W + x) * 2u;
            motion[oi]     = FloatToHalf(mx);
            motion[oi + 1] = FloatToHalf(my);
        }
    }

    cmd.UploadTextureData(output_motion_.Get(), motion.data(), W * 4u);
    fc.motion = output_motion_;
    fc.validity.motion_valid = true;
    return PassResult::Success;
}

}  // namespace omnirender::core::temporal
