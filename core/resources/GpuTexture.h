// filepath: core/resources/GpuTexture.h
#pragma once

#include <memory>
#include <cstdint>
#include "TextureDesc.h"
#include "TextureFormat.h"

namespace omnirender::graphics {
class IGraphicsTexture;
}

namespace omnirender::core {

class GpuTexture {
public:
    GpuTexture() = default;
    explicit GpuTexture(std::shared_ptr<graphics::IGraphicsTexture> texture) noexcept
        : texture_(std::move(texture)) {}

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] uint32_t GetWidth() const noexcept;
    [[nodiscard]] uint32_t GetHeight() const noexcept;
    [[nodiscard]] TextureFormat GetFormat() const noexcept;

    [[nodiscard]] graphics::IGraphicsTexture* Get() const noexcept { return texture_.get(); }
    [[nodiscard]] graphics::IGraphicsTexture* operator->() const noexcept { return texture_.get(); }
    [[nodiscard]] const std::shared_ptr<graphics::IGraphicsTexture>& Handle() const noexcept { return texture_; }

    void Reset() noexcept { texture_.reset(); }

    explicit operator bool() const noexcept { return IsValid(); }

private:
    std::shared_ptr<graphics::IGraphicsTexture> texture_;
};

}  // namespace omnirender::core
