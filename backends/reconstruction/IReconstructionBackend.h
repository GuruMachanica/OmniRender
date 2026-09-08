// filepath: backends/reconstruction/IReconstructionBackend.h
#pragma once

#include <string_view>
#include "../../core/frame/FrameContext.h"
#include "../../core/frame/Resolution.h"

namespace omnirender::graphics {
class IGraphicsDevice;
class ICommandContext;
}

namespace omnirender::backends {

struct ReconstructionResult {
    core::GpuTexture output;
    core::Resolution resolution;
    bool             success = false;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
    [[nodiscard]] bool operator!() const noexcept { return !success; }
};

class IReconstructionBackend {
public:
    virtual ~IReconstructionBackend() = default;

    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
    [[nodiscard]] virtual bool IsRuntimeAvailable() const noexcept = 0;

    virtual bool Initialize(graphics::IGraphicsDevice& device,
                            const core::Resolution& input_res,
                            const core::Resolution& output_res) = 0;

    virtual ReconstructionResult Execute(core::FrameContext& frame_ctx,
                                         graphics::ICommandContext& cmd_ctx) = 0;

    virtual void OnDeviceLost() = 0;
    virtual bool OnDeviceRestored(graphics::IGraphicsDevice& device) = 0;

    virtual void Shutdown() = 0;
};

}  // namespace omnirender::backends
