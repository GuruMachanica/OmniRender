// filepath: core/resources/TextureDesc.h
#pragma once

#include <cstdint>
#include "TextureFormat.h"

namespace omnirender::core {

enum class TextureUsage : uint16_t {
    None           = 0,
    ShaderResource = 1 << 0,
    RenderTarget   = 1 << 1,
    UnorderedAccess= 1 << 2,
    SharedHandle   = 1 << 3,
    TransferSrc    = 1 << 4,
    TransferDst    = 1 << 5
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

[[nodiscard]] constexpr bool HasFlag(TextureUsage mask, TextureUsage flag) noexcept {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(flag)) != 0;
}

struct TextureDesc {
    uint32_t      width      = 0;
    uint32_t      height     = 0;
    uint32_t      mip_levels = 1;
    TextureFormat format     = TextureFormat::Unknown;
    TextureUsage  usage      = TextureUsage::ShaderResource;
    const char*   debug_name = nullptr;
};

}  // namespace omnirender::core
