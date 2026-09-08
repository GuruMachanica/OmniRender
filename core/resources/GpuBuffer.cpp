// filepath: core/resources/GpuBuffer.cpp
#include "GpuBuffer.h"
#include "../../graphics/abstraction/IGraphicsBuffer.h"

namespace omnirender::core {

bool GpuBuffer::IsValid() const noexcept {
    return buffer_ && buffer_->IsValid();
}

uint32_t GpuBuffer::GetByteWidth() const noexcept {
    return buffer_ ? buffer_->GetByteWidth() : 0;
}

}  // namespace omnirender::core
