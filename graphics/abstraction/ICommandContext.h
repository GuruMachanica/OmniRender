// filepath: graphics/abstraction/ICommandContext.h
#pragma once

#include <cstdint>
#include <cstddef>
#include "IGraphicsTexture.h"
#include "IGraphicsBuffer.h"

namespace omnirender::graphics {

class ICommandContext {
public:
    virtual ~ICommandContext() = default;

    virtual bool BeginFrame() = 0;
    virtual bool EndFrame() = 0;

    virtual void SetComputeShader(void* native_shader) = 0;
    virtual void SetConstantBuffers(uint32_t start_slot, uint32_t count, IGraphicsBuffer* const* buffers) = 0;
    virtual void SetShaderResources(uint32_t start_slot, uint32_t count, IGraphicsTexture* const* textures) = 0;
    virtual void SetUnorderedAccessViews(uint32_t start_slot, uint32_t count, IGraphicsTexture* const* textures) = 0;

    virtual void CopyTexture(IGraphicsTexture* dst, IGraphicsTexture* src) = 0;
    virtual void UpdateBuffer(IGraphicsBuffer* buffer, const void* data, size_t size) = 0;
    // Upload CPU-side pixel data directly into a GPU texture (row_pitch in bytes).
    // Maps to UpdateSubresource on D3D11; used by CPU-fallback temporal passes.
    virtual void UploadTextureData(IGraphicsTexture* dst, const void* data, uint32_t row_pitch) = 0;
    // Read a GPU texture back into CPU memory. Returns false when readback is
    // unavailable (unsupported format, mock context, etc.). Used by the
    // DepthProvider CPU fallback; blocking sync is acceptable there because
    // it only runs when no GPU shader is loaded.
    virtual bool ReadbackTexture(IGraphicsTexture* src, void* out_data, size_t out_size) = 0;
    virtual void Dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) = 0;

    [[nodiscard]] virtual void* GetNativeContext() const noexcept = 0;
};

}  // namespace omnirender::graphics
