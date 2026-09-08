// filepath: graphics/abstraction/IGraphicsBuffer.h
#pragma once

#include <cstdint>
#include "../../core/resources/BufferDesc.h"

namespace omnirender::graphics {

class IGraphicsBuffer {
public:
    virtual ~IGraphicsBuffer() = default;

    [[nodiscard]] virtual const core::BufferDesc& GetDesc() const noexcept = 0;
    [[nodiscard]] virtual uint32_t GetByteWidth() const noexcept { return GetDesc().byte_width; }

    [[nodiscard]] virtual void* GetNativeResource() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeSrv() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeUav() const noexcept = 0;
    [[nodiscard]] virtual bool  IsValid() const noexcept = 0;
};

}  // namespace omnirender::graphics
