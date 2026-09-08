// filepath: graphics/d3d11/D3D11CommandContext.h
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "../abstraction/ICommandContext.h"

namespace omnirender::graphics::d3d11 {

class D3D11CommandContext final : public ICommandContext {
public:
    explicit D3D11CommandContext(Microsoft::WRL::ComPtr<ID3D11DeviceContext> context);
    ~D3D11CommandContext() override = default;

    bool BeginFrame() override;
    bool EndFrame() override;

    void SetComputeShader(void* native_shader) override;
    void SetConstantBuffers(uint32_t start_slot, uint32_t count, IGraphicsBuffer* const* buffers) override;
    void SetShaderResources(uint32_t start_slot, uint32_t count, IGraphicsTexture* const* textures) override;
    void SetUnorderedAccessViews(uint32_t start_slot, uint32_t count, IGraphicsTexture* const* textures) override;

    void CopyTexture(IGraphicsTexture* dst, IGraphicsTexture* src) override;
    void UpdateBuffer(IGraphicsBuffer* buffer, const void* data, size_t size) override;
    void UploadTextureData(IGraphicsTexture* dst, const void* data, uint32_t row_pitch) override;
    void Dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

    [[nodiscard]] void* GetNativeContext() const noexcept override { return context_.Get(); }
    [[nodiscard]] ID3D11DeviceContext* GetD3D11Context() const noexcept { return context_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
};

}  // namespace omnirender::graphics::d3d11
