// filepath: core/capability/GraphicsApi.h
#pragma once

#include <cstdint>
#include <string_view>

namespace omnirender::core {

enum class GraphicsApi : uint8_t {
    Unknown = 0,
    D3D9,
    D3D10,
    D3D11,
    D3D12,
    Vulkan,
    Metal,
    OpenGL
};

[[nodiscard]] constexpr std::string_view GraphicsApiToString(GraphicsApi api) noexcept {
    switch (api) {
        case GraphicsApi::D3D9:   return "Direct3D 9";
        case GraphicsApi::D3D10:  return "Direct3D 10";
        case GraphicsApi::D3D11:  return "Direct3D 11";
        case GraphicsApi::D3D12:  return "Direct3D 12";
        case GraphicsApi::Vulkan: return "Vulkan";
        case GraphicsApi::Metal:  return "Metal";
        case GraphicsApi::OpenGL: return "OpenGL";
        default:                  return "Unknown";
    }
}

}  // namespace omnirender::core
