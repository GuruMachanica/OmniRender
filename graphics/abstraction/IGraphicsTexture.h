// filepath: graphics/abstraction/IGraphicsTexture.h
#pragma once

#include <cstdint>
#include "../../core/resources/TextureDesc.h"

namespace omnirender::graphics {

class IGraphicsTexture {
public:
    virtual ~IGraphicsTexture() = default;

    [[nodiscard]] virtual const core::TextureDesc& GetDesc() const noexcept = 0;
    [[nodiscard]] virtual uint32_t GetWidth() const noexcept { return GetDesc().width; }
    [[nodiscard]] virtual uint32_t GetHeight() const noexcept { return GetDesc().height; }
    [[nodiscard]] virtual core::TextureFormat GetFormat() const noexcept { return GetDesc().format; }

    [[nodiscard]] virtual void* GetNativeResource() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeSrv() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeUav() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeRtv() const noexcept = 0;
    [[nodiscard]] virtual bool  IsValid() const noexcept = 0;
};

}  // namespace omnirender::graphics
