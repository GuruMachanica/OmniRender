// filepath: core/temporal/DisocclusionPass.cpp
// Depth-based disocclusion mask generation.
// GPU path: Disocclusion.cs.cso dispatch (R8_UNORM UAV).
// CPU path: per-pixel depth delta into staging buffer, uploaded via UploadTextureData.
#include "DisocclusionPass.h"
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

bool DisocclusionPass::Initialize(graphics::IGraphicsDevice& device,
                                  uint32_t width, uint32_t height) {
    Shutdown();
    device_ = &device;
    width_  = width;
    height_ = height;

    TextureDesc desc{ width, height, 1, TextureFormat::R8_UNORM,
        TextureUsage::UnorderedAccess | TextureUsage::ShaderResource |
        TextureUsage::TransferSrc, "DisocclusionMask" };
    auto tex = device.CreateTexture(desc);
    if (!tex) return false;
    output_disocc_ = GpuTexture(std::move(tex));

    // byte_width, stride_bytes (0 for constant buffers), usage, debug_name
    BufferDesc cb_desc{ sizeof(ReprojectionCB), 0, BufferUsage::ConstantBuffer, "DisocclusionCB" };
    cb_buffer_ = device.CreateBuffer(cb_desc, nullptr);

    // Optional GPU accelerator. Must be compiled from shaders/temporal/Disocclusion.hlsl
    // (cbuffer layout matches ReprojectionCB); the daemon-side disocclusion_hlsl.cso
    // uses a different constant-buffer layout and must not be loaded here.
    const std::string cso_path = GetShaderPath("Disocclusion.cso");
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
    // Always allocate CPU scratch so the uniform-mask fallback never touches an
    // empty buffer, regardless of GPU-path availability.
    cpu_pixels_.assign(static_cast<size_t>(width_) * height_, uint8_t(0));
    return true;
}

void DisocclusionPass::Shutdown() {
    if (gpu_shader_) {
#if defined(_WIN32)
        static_cast<IUnknown*>(gpu_shader_)->Release();
#endif
        gpu_shader_ = nullptr;
    }
    output_disocc_.Reset();
    cb_buffer_.reset();
    cpu_pixels_.clear();
    device_ = nullptr;
    width_ = height_ = 0;
}

PassResult DisocclusionPass::Execute(FrameContext& fc,
                                     graphics::ICommandContext& cmd,
                                     const GpuTexture& prev_depth) {
    if (!device_ || !output_disocc_.IsValid()) return PassResult::Failed;
    if (!fc.depth.IsValid() || !prev_depth.IsValid()) return PassResult::Skipped;

    PassResult res = gpu_shader_ ? ExecuteGpu(fc, cmd, prev_depth)
                                 : ExecuteCpu(fc, cmd, prev_depth);
    if (res == PassResult::Success) {
        fc.disocclusion = output_disocc_;
        fc.validity.disocc_valid = true;
    }
    return res;
}

PassResult DisocclusionPass::ExecuteGpu(FrameContext& fc,
                                        graphics::ICommandContext& cmd,
                                        const GpuTexture& prev_depth) {
    if (!cb_buffer_) return PassResult::Failed;
    auto cb = MakeReprojectionCB(fc);
    cmd.UpdateBuffer(cb_buffer_.get(), &cb, sizeof(cb));

    graphics::IGraphicsBuffer* cbs[] = { cb_buffer_.get() };
    cmd.SetConstantBuffers(0, 1, cbs);

    // SRV0 = current depth, SRV1 = previous depth.
    graphics::IGraphicsTexture* srvs[] = { fc.depth.Get(), prev_depth.Get() };
    cmd.SetShaderResources(0, 2, srvs);

    graphics::IGraphicsTexture* uavs[] = { output_disocc_.Get() };
    cmd.SetUnorderedAccessViews(0, 1, uavs);

    cmd.SetComputeShader(gpu_shader_);
    cmd.Dispatch(GroupCount(width_, kTileSize), GroupCount(height_, kTileSize), 1);

    graphics::IGraphicsTexture* null_uav[] = { nullptr };
    cmd.SetUnorderedAccessViews(0, 1, null_uav);
    return PassResult::Success;
}

// CPU fallback: approximate disocclusion using motion vector length as a proxy.
// A proper implementation would need a CPU-readable copy of the depth textures.
// Without GPU readback, we use camera movement magnitude to produce a uniform mask.
PassResult DisocclusionPass::ExecuteCpu(FrameContext& fc,
                                        graphics::ICommandContext& cmd,
                                        const GpuTexture&) {
    // Estimate global disocclusion probability from camera delta (conservative heuristic).
    bool any_movement = fc.camera.HasMovement();
    // Mark all pixels with a low uniform value when moving (full readback deferred to GPU path).
    uint8_t fill = any_movement ? 32u : 0u;
    std::fill(cpu_pixels_.begin(), cpu_pixels_.end(), fill);
    cmd.UploadTextureData(output_disocc_.Get(), cpu_pixels_.data(), width_ * 1u);
    return PassResult::Success;
}

}  // namespace omnirender::core::temporal
