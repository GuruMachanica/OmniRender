// filepath: core/resources/TextureFormat.h
#pragma once

#include <cstdint>
#include <string_view>

namespace omnirender::core {

enum class TextureFormat : uint8_t {
    Unknown = 0,
    R8_UNORM,
    R8G8B8A8_UNORM,
    R8G8B8A8_UNORM_SRGB,
    B8G8R8A8_UNORM,
    R10G10B10A2_UNORM,
    R16G16_FLOAT,
    R16G16B16A16_FLOAT,
    R32_FLOAT,
    R32G32_FLOAT,
    R32G32B32A32_FLOAT,
    D16_UNORM,
    D24_UNORM_S8_UINT,
    D32_FLOAT
};

[[nodiscard]] constexpr uint32_t GetBytesPerPixel(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::R8_UNORM:             return 1;
        case TextureFormat::D16_UNORM:            return 2;
        case TextureFormat::R8G8B8A8_UNORM:
        case TextureFormat::R8G8B8A8_UNORM_SRGB:
        case TextureFormat::B8G8R8A8_UNORM:
        case TextureFormat::R10G10B10A2_UNORM:
        case TextureFormat::R16G16_FLOAT:
        case TextureFormat::R32_FLOAT:
        case TextureFormat::D24_UNORM_S8_UINT:
        case TextureFormat::D32_FLOAT:             return 4;
        case TextureFormat::R16G16B16A16_FLOAT:
        case TextureFormat::R32G32_FLOAT:          return 8;
        case TextureFormat::R32G32B32A32_FLOAT:    return 16;
        default:                                   return 0;
    }
}

}  // namespace omnirender::core
