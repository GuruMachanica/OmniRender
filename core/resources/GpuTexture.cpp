// filepath: core/resources/GpuTexture.cpp
#include "GpuTexture.h"
#include "../../graphics/abstraction/IGraphicsTexture.h"

namespace omnirender::core {

bool GpuTexture::IsValid() const noexcept {
    return texture_ && texture_->IsValid();
}

uint32_t GpuTexture::GetWidth() const noexcept {
    return texture_ ? texture_->GetWidth() : 0;
}

uint32_t GpuTexture::GetHeight() const noexcept {
    return texture_ ? texture_->GetHeight() : 0;
}

TextureFormat GpuTexture::GetFormat() const noexcept {
    return texture_ ? texture_->GetFormat() : TextureFormat::Unknown;
}

}  // namespace omnirender::core
