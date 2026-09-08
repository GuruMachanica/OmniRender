// filepath: core/temporal/MotionReprojectionPass.h
// Depth-reprojected motion vector generation pass.
// Dual-path: GPU compute (preferred) / CPU fallback.
#pragma once

#include <memory>
#include <vector>
#include "TemporalPassBase.h"
#include "../graph/RenderGraphTypes.h"
#include "../resources/GpuTexture.h"

namespace omnirender::graphics {
class IGraphicsDevice;
class ICommandContext;
class IGraphicsBuffer;
}

namespace omnirender::core::temporal {

class MotionReprojectionPass {
public:
    MotionReprojectionPass() = default;
    ~MotionReprojectionPass() { Shutdown(); }

    MotionReprojectionPass(const MotionReprojectionPass&) = delete;
    MotionReprojectionPass& operator=(const MotionReprojectionPass&) = delete;

    // Initialize GPU resources.  device must outlive this object.
    // Returns true even if shader loading fails (CPU fallback active).
    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t width, uint32_t height);
    void Shutdown();

    // Execute: writes NDC motion vectors into fc.motion.
    // Returns Success, Skipped (no depth/history), or Failed.
    PassResult Execute(FrameContext& fc, graphics::ICommandContext& cmd);

    [[nodiscard]] bool IsGpuPathActive() const noexcept { return gpu_shader_ != nullptr; }

private:
    PassResult ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd);
    PassResult ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd);

    GpuTexture                                   output_motion_;
    std::shared_ptr<graphics::IGraphicsBuffer>   cb_buffer_;
    void*                                        gpu_shader_ = nullptr; // ID3D11ComputeShader*
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    // RG16F staging scratch (2 × uint16_t per pixel, row_pitch = width*4 bytes).
    // Must be RG16F to match the R16G16_FLOAT UAV texture format.
    std::vector<uint16_t>                        cpu_pixels_;
    graphics::IGraphicsDevice*                   device_ = nullptr;

    // IEEE 754 binary16 conversion (software half-float).
    static uint16_t FloatToHalf(float f) noexcept;
};

}  // namespace omnirender::core::temporal
