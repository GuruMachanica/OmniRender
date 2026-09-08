// filepath: graphics/d3d11/D3D11GraphicsTexture.cpp
#include "D3D11GraphicsTexture.h"
#include <utility>

namespace omnirender::graphics::d3d11 {

D3D11GraphicsTexture::D3D11GraphicsTexture(const core::TextureDesc& desc,
                                           Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
                                           Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv,
                                           Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav,
                                           Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv)
    : desc_(desc),
      texture_(std::move(texture)),
      srv_(std::move(srv)),
      uav_(std::move(uav)),
      rtv_(std::move(rtv)) {
}

}  // namespace omnirender::graphics::d3d11
