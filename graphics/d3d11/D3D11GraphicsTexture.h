// filepath: graphics/d3d11/D3D11GraphicsTexture.h
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "../abstraction/IGraphicsTexture.h"

namespace omnirender::graphics::d3d11 {

class D3D11GraphicsTexture final : public IGraphicsTexture {
public:
    D3D11GraphicsTexture(const core::TextureDesc& desc,
                         Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv = nullptr,
                         Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav = nullptr,
                         Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv = nullptr);

    ~D3D11GraphicsTexture() override = default;

    [[nodiscard]] const core::TextureDesc& GetDesc() const noexcept override { return desc_; }
    [[nodiscard]] void* GetNativeResource() const noexcept override { return texture_.Get(); }
    [[nodiscard]] void* GetNativeSrv() const noexcept override { return srv_.Get(); }
    [[nodiscard]] void* GetNativeUav() const noexcept override { return uav_.Get(); }
    [[nodiscard]] void* GetNativeRtv() const noexcept override { return rtv_.Get(); }
    [[nodiscard]] bool  IsValid() const noexcept override { return texture_ != nullptr; }

    [[nodiscard]] ID3D11Texture2D*          GetD3D11Texture() const noexcept { return texture_.Get(); }
    [[nodiscard]] ID3D11ShaderResourceView*  GetD3D11Srv() const noexcept { return srv_.Get(); }
    [[nodiscard]] ID3D11UnorderedAccessView* GetD3D11Uav() const noexcept { return uav_.Get(); }
    [[nodiscard]] ID3D11RenderTargetView*    GetD3D11Rtv() const noexcept { return rtv_.Get(); }

private:
    core::TextureDesc desc_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  srv_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    rtv_;
};

}  // namespace omnirender::graphics::d3d11
