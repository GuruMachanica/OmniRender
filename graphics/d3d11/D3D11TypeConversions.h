// filepath: graphics/d3d11/D3D11TypeConversions.h
#pragma once

#include <d3d11.h>
#include <dxgiformat.h>
#include "../../core/resources/TextureFormat.h"
#include "../../core/resources/TextureDesc.h"
#include "../../core/resources/BufferDesc.h"

namespace omnirender::graphics::d3d11 {

[[nodiscard]] DXGI_FORMAT ToDxgiFormat(core::TextureFormat format) noexcept;
[[nodiscard]] core::TextureFormat FromDxgiFormat(DXGI_FORMAT format) noexcept;

[[nodiscard]] UINT ToD3D11BindFlags(core::TextureUsage usage) noexcept;
[[nodiscard]] UINT ToD3D11BindFlags(core::BufferUsage usage) noexcept;

}  // namespace omnirender::graphics::d3d11
