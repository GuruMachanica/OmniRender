// filepath: core/temporal/ReactiveMaskPass.cpp
// Heuristic reactive mask generation.
// GPU path: ReactiveMask.cs.cso dispatch (R8_UNORM UAV).
// CPU path: produces a conservative uniform reactive mask (no GPU readback).
#include "ReactiveMaskPass.h"
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

bool ReactiveMaskPass::Initialize(graphics::IGraphicsDevice& device,
                                  uint32_t width, uint32_t height) {
    Shutdown();
    device_ = &device;
    width_  = width;
    height_ = height;

    TextureDesc desc{ width, height, 1, TextureFormat::R8_UNORM,
        TextureUsage::UnorderedAccess | TextureUsage::ShaderResource |
        TextureUsage::TransferSrc, "ReactiveMask" };
    auto tex = device.CreateTexture(desc);
    if (!tex) return false;
    output_reactive_ = GpuTexture(std::move(tex));

    // byte_width, stride_bytes (0 for constant buffers), usage, debug_name
    BufferDesc cb_desc{ sizeof(ReprojectionCB), 0, BufferUsage::ConstantBuffer, "ReactiveCB" };
    cb_buffer_ = device.CreateBuffer(cb_desc, nullptr);

    // Optional GPU accelerator. Must be compiled from shaders/temporal/ReactiveMask.hlsl
    // (cbuffer layout matches ReprojectionCB); the daemon-side reactive_mask_hlsl.cso
    // uses a different constant-buffer layout and must not be loaded here.
    const std::string cso_path = GetShaderPath("ReactiveMask.cso");
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
    // Always allocate CPU scratch so the neutral-mask fallback never touches an
    // empty buffer, regardless of GPU-path availability.
    cpu_pixels_.assign(static_cast<size_t>(width_) * height_, uint8_t(0));
    return true;
}

void ReactiveMaskPass::Shutdown() {
    if (gpu_shader_) {
#if defined(_WIN32)
        static_cast<IUnknown*>(gpu_shader_)->Release();
#endif
        gpu_shader_ = nullptr;
    }
    output_reactive_.Reset();
    cb_buffer_.reset();
    cpu_pixels_.clear();
    device_ = nullptr;
    width_ = height_ = 0;
}

PassResult ReactiveMaskPass::Execute(FrameContext& fc,
                                     graphics::ICommandContext& cmd,
                                     const GpuTexture& history_color) {
    if (!device_ || !output_reactive_.IsValid()) return PassResult::Failed;

    PassResult res;
    if (gpu_shader_ && history_color.IsValid()) {
        res = ExecuteGpu(fc, cmd, history_color);
    } else {
        // No history yet (first frame) or no GPU shader: CPU fallback with neutral mask.
        res = ExecuteCpu(fc, cmd);
    }
    if (res == PassResult::Success) {
        fc.reactive = output_reactive_;
        fc.validity.reactive_valid = true;
    }
    return res;
}

PassResult ReactiveMaskPass::ExecuteGpu(FrameContext& fc,
                                        graphics::ICommandContext& cmd,
                                        const GpuTexture& history_color) {
    if (!cb_buffer_) return PassResult::Failed;
    auto cb = MakeReprojectionCB(fc);
    cmd.UpdateBuffer(cb_buffer_.get(), &cb, sizeof(cb));

    graphics::IGraphicsBuffer* cbs[] = { cb_buffer_.get() };
    cmd.SetConstantBuffers(0, 1, cbs);

    // SRV0 = current color, SRV1 = history color.
    graphics::IGraphicsTexture* srvs[] = { fc.color.Get(), history_color.Get() };
    cmd.SetShaderResources(0, 2, srvs);

    graphics::IGraphicsTexture* uavs[] = { output_reactive_.Get() };
    cmd.SetUnorderedAccessViews(0, 1, uavs);

    cmd.SetComputeShader(gpu_shader_);
    cmd.Dispatch(GroupCount(width_, kTileSize), GroupCount(height_, kTileSize), 1);

    graphics::IGraphicsTexture* null_uav[] = { nullptr };
    cmd.SetUnorderedAccessViews(0, 1, null_uav);
    return PassResult::Success;
}

// CPU fallback: output a neutral (zero) reactive mask.
// DLSS treats 0 = stable (no special treatment), so this is safe for the first frame.
PassResult ReactiveMaskPass::ExecuteCpu(FrameContext& fc,
                                        graphics::ICommandContext& cmd) {
    std::fill(cpu_pixels_.begin(), cpu_pixels_.end(), 0u);
    cmd.UploadTextureData(output_reactive_.Get(), cpu_pixels_.data(), width_ * 1u);
    return PassResult::Success;
}

}  // namespace omnirender::core::temporal
