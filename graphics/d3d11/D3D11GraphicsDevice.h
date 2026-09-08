// filepath: graphics/d3d11/D3D11GraphicsDevice.h
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "../abstraction/IGraphicsDevice.h"
#include "D3D11CommandContext.h"

namespace omnirender::graphics::d3d11 {

class D3D11GraphicsDevice final : public IGraphicsDevice {
public:
    explicit D3D11GraphicsDevice(Microsoft::WRL::ComPtr<ID3D11Device> device,
                                 Microsoft::WRL::ComPtr<ID3D11DeviceContext> context = nullptr);
    ~D3D11GraphicsDevice() override = default;

    [[nodiscard]] core::GraphicsApi GetApi() const noexcept override { return core::GraphicsApi::D3D11; }

    [[nodiscard]] std::shared_ptr<IGraphicsTexture> CreateTexture(const core::TextureDesc& desc) override;
    [[nodiscard]] std::shared_ptr<IGraphicsTexture> OpenSharedTexture(uint64_t shared_handle) override;

    [[nodiscard]] std::shared_ptr<IGraphicsBuffer> CreateBuffer(const core::BufferDesc& desc, const void* initial_data) override;

    [[nodiscard]] std::shared_ptr<ICommandContext> GetImmediateContext() override;

    [[nodiscard]] void* GetNativeDevice() const noexcept override { return device_.Get(); }
    [[nodiscard]] ID3D11Device* GetD3D11Device() const noexcept { return device_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Device>        device_;
    std::shared_ptr<D3D11CommandContext>        immediate_context_;
};

}  // namespace omnirender::graphics::d3d11
