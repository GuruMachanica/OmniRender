// filepath: graphics/abstraction/IGraphicsDevice.h
#pragma once

#include <memory>
#include <cstdint>
#include "IGraphicsTexture.h"
#include "IGraphicsBuffer.h"
#include "ICommandContext.h"
#include "../../core/capability/GraphicsApi.h"

namespace omnirender::graphics {

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;

    [[nodiscard]] virtual core::GraphicsApi GetApi() const noexcept = 0;

    [[nodiscard]] virtual std::shared_ptr<IGraphicsTexture> CreateTexture(const core::TextureDesc& desc) = 0;
    [[nodiscard]] virtual std::shared_ptr<IGraphicsTexture> OpenSharedTexture(uint64_t shared_handle) = 0;

    [[nodiscard]] virtual std::shared_ptr<IGraphicsBuffer> CreateBuffer(const core::BufferDesc& desc, const void* initial_data) = 0;

    [[nodiscard]] virtual std::shared_ptr<ICommandContext> GetImmediateContext() = 0;

    [[nodiscard]] virtual void* GetNativeDevice() const noexcept = 0;
};

}  // namespace omnirender::graphics
