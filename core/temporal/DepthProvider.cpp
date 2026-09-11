// filepath: core/temporal/DepthProvider.cpp
// Depth linearization (raw game depth -> linearized [0,1] view depth).
// GPU path (optional): DepthLinearize.cso dispatch, R32F -> R32F.
// CPU fallback: staging readback + per-pixel perspective linearization.
#include "DepthProvider.h"
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

bool DepthProvider::Initialize(graphics::IGraphicsDevice& device,
                               uint32_t width, uint32_t height) {
    Shutdown();
    device_ = &device;
    width_  = width;
    height_ = height;

    TextureDesc desc{ width, height, 1, TextureFormat::R32_FLOAT,
        TextureUsage::ShaderResource | TextureUsage::UnorderedAccess |
        TextureUsage::TransferSrc | TextureUsage::TransferDst,
        "LinearizedDepth" };
    auto tex = device.CreateTexture(desc);
    if (!tex) return false;
    output_linear_ = GpuTexture(std::move(tex));

    // byte_width, stride_bytes (0 for constant buffers), usage, debug_name
    BufferDesc cb_desc{ sizeof(ReprojectionCB), 0, BufferUsage::ConstantBuffer, "DepthLinearizeCB" };
    cb_buffer_ = device.CreateBuffer(cb_desc, nullptr);

    // Optional GPU accelerator, compiled from shaders/temporal/DepthLinearize.hlsl.
    // Its cbuffer layout matches ReprojectionCB (g_Pad doubles as the
    // reversed-Z flag). ShaderPath resolves relative to the running
    // executable; when the CSO is absent the CPU fallback stays active.
    const std::string cso_path = GetShaderPath("DepthLinearize.cso");
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
    // Bounded staging scratch: exactly one input-resolution R32F image.
    raw_pixels_.assign(static_cast<size_t>(width_) * height_ * 4u, 0u);
    return true;
}

void DepthProvider::Shutdown() {
    if (gpu_shader_) {
#if defined(_WIN32)
        static_cast<IUnknown*>(gpu_shader_)->Release();
#endif
        gpu_shader_ = nullptr;
    }
    output_linear_.Reset();
    cb_buffer_.reset();
    raw_pixels_.clear();
    device_ = nullptr;
    width_ = height_ = 0;
}

PassResult DepthProvider::Process(FrameContext& fc, graphics::ICommandContext& cmd) {
    if (!device_ || !output_linear_.IsValid()) return PassResult::Failed;
    if (!fc.depth.IsValid()) return PassResult::Skipped;

    // Resolution-domain guard (audit plan Commit 5): refuse to read/write
    // across mismatched domains instead of dispatching out of bounds.
    if (fc.depth.GetWidth() != width_ || fc.depth.GetHeight() != height_) {
        return PassResult::Skipped;
    }

    PassResult res = gpu_shader_ ? ExecuteGpu(fc, cmd) : ExecuteCpu(fc, cmd);
    if (res == PassResult::Success) {
        fc.depth_linear = output_linear_;
        fc.validity.depth_linear_valid = true;
    }
    return res;
}

PassResult DepthProvider::ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd) {
    if (!cb_buffer_) return PassResult::Failed;
    auto cb = MakeReprojectionCB(fc);
    // ReprojectionCB._pad carries the reversed-Z flag into the shader
    // (g_ReverseZ). The camera flag is the source of truth; the IPC
    // ReversedZ flag was already folded into fc.camera.is_reverse_z.
    cb._pad = fc.camera.is_reverse_z ? 1.0f : 0.0f;
    cmd.UpdateBuffer(cb_buffer_.get(), &cb, sizeof(cb));

    graphics::IGraphicsBuffer* cbs[] = { cb_buffer_.get() };
    cmd.SetConstantBuffers(0, 1, cbs);

    graphics::IGraphicsTexture* srvs[] = { fc.depth.Get() };
    cmd.SetShaderResources(0, 1, srvs);

    graphics::IGraphicsTexture* uavs[] = { output_linear_.Get() };
    cmd.SetUnorderedAccessViews(0, 1, uavs);

    cmd.SetComputeShader(gpu_shader_);
    cmd.Dispatch(GroupCount(width_, kTileSize), GroupCount(height_, kTileSize), 1);

    graphics::IGraphicsTexture* null_uav[] = { nullptr };
    cmd.SetUnorderedAccessViews(0, 1, null_uav);
    return PassResult::Success;
}

// CPU fallback: staging readback of the raw depth, per-pixel linearization,
// upload into the linear output. Works on WARP; costs one GPU sync/frame.
PassResult DepthProvider::ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd) {
    const size_t need = static_cast<size_t>(width_) * height_ * 4u;
    if (raw_pixels_.size() != need) raw_pixels_.assign(need, 0u);

    if (!cmd.ReadbackTexture(fc.depth.Get(), raw_pixels_.data(), raw_pixels_.size())) {
        return PassResult::Skipped;  // readback unavailable (mock contexts)
    }

    const float n = (fc.camera.near_z > 0.0f) ? fc.camera.near_z : 0.1f;
    const float f = (fc.camera.far_z > n) ? fc.camera.far_z : n + 1.0f;
    const bool  reversed = fc.camera.is_reverse_z;

    const uint32_t W = width_, H = height_;
    for (uint32_t y = 0; y < H; ++y) {
        float* row = reinterpret_cast<float*>(raw_pixels_.data() + static_cast<size_t>(y) * W * 4u);
        for (uint32_t x = 0; x < W; ++x) {
            float raw = row[x];
            if (!(raw >= 0.0f) || raw > 1.0f) raw = (raw > 1.0f) ? 1.0f : 0.0f;  // clamp NaN/inf/overflow
            if (reversed) raw = 1.0f - raw;
            const float viewZ  = (n * f) / (f - raw * (f - n) + 1e-9f);
            row[x] = (viewZ - n) / (f - n);
        }
    }

    cmd.UploadTextureData(output_linear_.Get(), raw_pixels_.data(), W * 4u);
    return PassResult::Success;
}

}  // namespace omnirender::core::temporal
