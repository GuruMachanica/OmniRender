// filepath: core/resources/GpuBuffer.h
#pragma once

#include <memory>
#include <cstdint>
#include "BufferDesc.h"

namespace omnirender::graphics {
class IGraphicsBuffer;
}

namespace omnirender::core {

class GpuBuffer {
public:
    GpuBuffer() = default;
    explicit GpuBuffer(std::shared_ptr<graphics::IGraphicsBuffer> buffer) noexcept
        : buffer_(std::move(buffer)) {}

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] uint32_t GetByteWidth() const noexcept;

    [[nodiscard]] graphics::IGraphicsBuffer* Get() const noexcept { return buffer_.get(); }
    [[nodiscard]] graphics::IGraphicsBuffer* operator->() const noexcept { return buffer_.get(); }
    [[nodiscard]] const std::shared_ptr<graphics::IGraphicsBuffer>& Handle() const noexcept { return buffer_; }

    void Reset() noexcept { buffer_.reset(); }

    explicit operator bool() const noexcept { return IsValid(); }

private:
    std::shared_ptr<graphics::IGraphicsBuffer> buffer_;
};

}  // namespace omnirender::core
