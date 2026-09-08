// filepath: graphics/d3d11/D3D11GraphicsBuffer.cpp
#include "D3D11GraphicsBuffer.h"
#include <utility>

namespace omnirender::graphics::d3d11 {

D3D11GraphicsBuffer::D3D11GraphicsBuffer(const core::BufferDesc& desc,
                                         Microsoft::WRL::ComPtr<ID3D11Buffer> buffer,
                                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv,
                                         Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav)
    : desc_(desc),
      buffer_(std::move(buffer)),
      srv_(std::move(srv)),
      uav_(std::move(uav)) {
}

}  // namespace omnirender::graphics::d3d11
