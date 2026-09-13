// filepath: core/temporal/OpticalFlowPass.h
// Screen-space optical flow fallback for motion vector generation.
//
// The pipeline's primary motion source is depth + camera reprojection
// (MotionReprojectionPass). But two real classes of frames cannot produce
// reprojection motion:
//   - DXGI (D3D10/11) games: the hook has no GetTransform equivalent, so
//     view_proj matrices are never extracted (CameraZero).
//   - GL games that upload matrices through shader uniforms only, never
//     touching the fixed-function stacks.
// For those, this pass estimates motion between the current frame and the
// committed history color with a bounded search-window block search, and
// publishes it in the SAME NDC (current - previous) convention the
// MotionReprojectionPass uses, so DLSS/XeSS/Disocclusion consume it
// unchanged.
//
// Per audit #20 this is a FALLBACK source, never the primary one: the pass
// runs only when fc.motion is still invalid after the reprojection pass.
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

class OpticalFlowPass {
public:
    OpticalFlowPass() = default;
    ~OpticalFlowPass() { Shutdown(); }

    OpticalFlowPass(const OpticalFlowPass&) = delete;
    OpticalFlowPass& operator=(const OpticalFlowPass&) = delete;

    // Allocates the RG16F motion output (+ optional CSO load). Returns true
    // even if shader loading fails (CPU fallback active).
    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t width, uint32_t height);
    void Shutdown();

    // Estimate motion from fc.color against `previous_color` (the committed
    // history). Writes fc.motion in the pipeline NDC convention when it is
    // not already valid; returns Skipped when motion already exists, color
    // or history is missing, or dimensions mismatch.
    PassResult Execute(FrameContext& fc, graphics::ICommandContext& cmd,
                       const GpuTexture& previous_color);

    [[nodiscard]] bool IsGpuPathActive() const noexcept { return gpu_shader_ != nullptr; }

    // Last written motion texture (RG16F, NDC current-previous). Used by the
    // F12 frame debugger.
    [[nodiscard]] const GpuTexture& GetOutput() const noexcept { return output_motion_; }

    // Half-extent of the search window in pixels (default 8 => 17x17 search).
    // Larger windows cost linearly in the compute shader and catch faster
    // motion; 8 covers ~480 px/s at 60 fps.
    uint32_t search_radius = 8;

private:
    PassResult ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd,
                          const GpuTexture& previous_color);
    PassResult ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd,
                          const GpuTexture& previous_color);

    GpuTexture                                  output_motion_;
    std::shared_ptr<graphics::IGraphicsBuffer>  cb_buffer_;
    void*                                       gpu_shader_ = nullptr; // ID3D11ComputeShader*
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    // RGBA8 color readback scratch (two frames: current + previous) for the
    // CPU fallback path. Bounded: 2x one input-resolution RGBA8 image.
    std::vector<uint8_t>                        cpu_curr_;
    std::vector<uint8_t>                        cpu_prev_;
    graphics::IGraphicsDevice*                  device_ = nullptr;

    static uint16_t FloatToHalf(float f) noexcept;
};

}  // namespace omnirender::core::temporal
