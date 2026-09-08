// filepath: graphics/d3d11/D3D11TypeConversions.cpp
#include "D3D11TypeConversions.h"

namespace omnirender::graphics::d3d11 {

DXGI_FORMAT ToDxgiFormat(core::TextureFormat format) noexcept {
    switch (format) {
        case core::TextureFormat::R8_UNORM:             return DXGI_FORMAT_R8_UNORM;
        case core::TextureFormat::R8G8B8A8_UNORM:        return DXGI_FORMAT_R8G8B8A8_UNORM;
        case core::TextureFormat::R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case core::TextureFormat::B8G8R8A8_UNORM:        return DXGI_FORMAT_B8G8R8A8_UNORM;
        case core::TextureFormat::R10G10B10A2_UNORM:     return DXGI_FORMAT_R10G10B10A2_UNORM;
        case core::TextureFormat::R16G16_FLOAT:          return DXGI_FORMAT_R16G16_FLOAT;
        case core::TextureFormat::R16G16B16A16_FLOAT:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case core::TextureFormat::R32_FLOAT:             return DXGI_FORMAT_R32_FLOAT;
        case core::TextureFormat::R32G32_FLOAT:          return DXGI_FORMAT_R32G32_FLOAT;
        case core::TextureFormat::R32G32B32A32_FLOAT:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case core::TextureFormat::D16_UNORM:             return DXGI_FORMAT_D16_UNORM;
        case core::TextureFormat::D24_UNORM_S8_UINT:     return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case core::TextureFormat::D32_FLOAT:             return DXGI_FORMAT_D32_FLOAT;
        default:                                         return DXGI_FORMAT_UNKNOWN;
    }
}

core::TextureFormat FromDxgiFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
        case DXGI_FORMAT_R8_UNORM:             return core::TextureFormat::R8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM:        return core::TextureFormat::R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return core::TextureFormat::R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:        return core::TextureFormat::B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_UNORM:     return core::TextureFormat::R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16_FLOAT:          return core::TextureFormat::R16G16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:    return core::TextureFormat::R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32_FLOAT:             return core::TextureFormat::R32_FLOAT;
        case DXGI_FORMAT_R32G32_FLOAT:          return core::TextureFormat::R32G32_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:    return core::TextureFormat::R32G32B32A32_FLOAT;
        case DXGI_FORMAT_D16_UNORM:             return core::TextureFormat::D16_UNORM;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:     return core::TextureFormat::D24_UNORM_S8_UINT;
        case DXGI_FORMAT_D32_FLOAT:             return core::TextureFormat::D32_FLOAT;
        default:                                return core::TextureFormat::Unknown;
    }
}

UINT ToD3D11BindFlags(core::TextureUsage usage) noexcept {
    UINT flags = 0;
    if (core::HasFlag(usage, core::TextureUsage::ShaderResource))  flags |= D3D11_BIND_SHADER_RESOURCE;
    if (core::HasFlag(usage, core::TextureUsage::RenderTarget))    flags |= D3D11_BIND_RENDER_TARGET;
    if (core::HasFlag(usage, core::TextureUsage::UnorderedAccess)) flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

UINT ToD3D11BindFlags(core::BufferUsage usage) noexcept {
    UINT flags = 0;
    if (core::HasFlag(usage, core::BufferUsage::ConstantBuffer)) flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (core::HasFlag(usage, core::BufferUsage::VertexBuffer))   flags |= D3D11_BIND_VERTEX_BUFFER;
    if (core::HasFlag(usage, core::BufferUsage::IndexBuffer))    flags |= D3D11_BIND_INDEX_BUFFER;
    if (core::HasFlag(usage, core::BufferUsage::UnorderedAccess))flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

}  // namespace omnirender::graphics::d3d11
