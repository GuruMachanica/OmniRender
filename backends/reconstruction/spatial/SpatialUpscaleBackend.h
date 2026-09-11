// filepath: backends/reconstruction/spatial/SpatialUpscaleBackend.h
// FSR 1.0 (EASU + RCAS) spatial upscaling backend for the core runtime pipeline.
//
// Loads the already-compiled fsr_easu.cso / fsr_rcas.cso compute shaders
// (built by the daemon shader target from modules/shaders/fsr_easu.hlsl and
// fsr_rcas.hlsl) and dispatches them through the core command context.
//
// This is the non-DLSS upscaler for the runtime path. It allocates exactly
// two output-resolution textures and two constant buffers — nothing
// full-res beyond the output itself, so VRAM stays bounded by construction.
//
// Dispatch model (matches the compiled CSOs):
//   EASU: t0 = input color   -> u0 = easu_target   (b0 = FsrEasuConstants)
//   RCAS: t0 = easu_target   -> u0 = rcas_target   (b0 = FsrRcasConstants)
// Two separate textures because D3D11 forbids binding the same resource as
// SRV and UAV simultaneously in one dispatch.

#pragma once

#if defined(_WIN32)

#include <d3d11.h>
#include <memory>
#include <string>
#include "../IReconstructionBackend.h"
#include "../../../core/resources/GpuTexture.h"
#include "../../../core/frame/Resolution.h"

namespace omnirender::backends::spatial {

class SpatialUpscaleBackend final : public IReconstructionBackend {
public:
    SpatialUpscaleBackend() = default;
    ~SpatialUpscaleBackend() override;

    SpatialUpscaleBackend(const SpatialUpscaleBackend&) = delete;
    SpatialUpscaleBackend& operator=(const SpatialUpscaleBackend&) = delete;

    const char* GetName() const override { return "FSR-Spatial"; }

    bool Initialize(graphics::IGraphicsDevice& device,
                    const core::Resolution& in_res,
                    const core::Resolution& out_res) override;
    void Shutdown() override;
    void OnDeviceLost() override;
    bool OnDeviceRestored(graphics::IGraphicsDevice& device) override;

    bool IsRuntimeAvailable() const override { return runtime_available_; }
    bool Resize(const core::Resolution& in_res, const core::Resolution& out_res);

    ReconstructionResult Execute(core::FrameContext& fc,
                                 graphics::ICommandContext& ctx) override;

    // RCAS sharpening strength [0, 1]. 0.75 matches the legacy daemon path.
    void SetSharpness(float sharpness) noexcept { sharpness_ = sharpness; }

    // Diagnostics from the last Initialize()/LoadShaders() failure.
    [[nodiscard]] const std::string& LastErrorString() const noexcept { return last_error_; }

private:
    bool LoadShaders();
    bool EnsureTargets(graphics::IGraphicsDevice& device);
    void ReleaseShaders();

    graphics::IGraphicsDevice* device_ = nullptr;
    core::Resolution input_res_{};
    core::Resolution output_res_{};
    core::GpuTexture easu_target_{};   // EASU UAV destination, RCAS SRV source
    core::GpuTexture rcas_target_{};   // RCAS UAV destination = final output
    std::shared_ptr<graphics::IGraphicsBuffer> easu_cb_;
    std::shared_ptr<graphics::IGraphicsBuffer> rcas_cb_;
    ID3D11ComputeShader* easu_cs_ = nullptr;
    ID3D11ComputeShader* rcas_cs_ = nullptr;
    float sharpness_ = 0.75f;
    bool runtime_available_ = false;
    std::string last_error_;
};

}  // namespace omnirender::backends::spatial

#endif  // _WIN32
