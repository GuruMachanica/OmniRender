// filepath: graphics/d3d11/D3D11GraphicsBuffer.h
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "../abstraction/IGraphicsBuffer.h"

namespace omnirender::graphics::d3d11 {

class D3D11GraphicsBuffer final : public IGraphicsBuffer {
public:
    D3D11GraphicsBuffer(const core::BufferDesc& desc,
                        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer,
                        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv = nullptr,
                        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav = nullptr);

    ~D3D11GraphicsBuffer() override = default;

    [[nodiscard]] const core::BufferDesc& GetDesc() const noexcept override { return desc_; }
    [[nodiscard]] void* GetNativeResource() const noexcept override { return buffer_.Get(); }
    [[nodiscard]] void* GetNativeSrv() const noexcept override { return srv_.Get(); }
    [[nodiscard]] void* GetNativeUav() const noexcept override { return uav_.Get(); }
    [[nodiscard]] bool  IsValid() const noexcept override { return buffer_ != nullptr; }

    [[nodiscard]] ID3D11Buffer*              GetD3D11Buffer() const noexcept { return buffer_.Get(); }
    [[nodiscard]] ID3D11ShaderResourceView*  GetD3D11Srv() const noexcept { return srv_.Get(); }
    [[nodiscard]] ID3D11UnorderedAccessView* GetD3D11Uav() const noexcept { return uav_.Get(); }

private:
    core::BufferDesc desc_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              buffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  srv_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_;
};

}  // namespace omnirender::graphics::d3d11
