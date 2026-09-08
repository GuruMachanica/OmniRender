// filepath: core/frame/Resolution.h
#pragma once

#include <cstdint>

namespace omnirender::core {

struct Resolution {
    uint32_t width  = 0;
    uint32_t height = 0;

    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        return width == 0 || height == 0;
    }

    [[nodiscard]] constexpr float AspectRatio() const noexcept {
        return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
    }

    [[nodiscard]] constexpr bool Is16By9() const noexcept {
        return height > 0 && (width * 9 == height * 16);
    }

    [[nodiscard]] constexpr float ScaleFactorFrom(const Resolution& other) const noexcept {
        return other.width > 0 ? static_cast<float>(width) / static_cast<float>(other.width) : 1.0f;
    }

    [[nodiscard]] constexpr bool operator==(const Resolution& other) const noexcept {
        return width == other.width && height == other.height;
    }

    [[nodiscard]] constexpr bool operator!=(const Resolution& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace omnirender::core
