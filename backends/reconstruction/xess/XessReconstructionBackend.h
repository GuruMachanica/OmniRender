// filepath: backends/reconstruction/xess/XessReconstructionBackend.h
// Intel XeSS neural reconstruction backend for the core runtime pipeline.
//
// Self-contained dynamic loader for libxess_dx11.dll — no SDK headers or
// import libraries at build time. The packed(8) parameter structs mirror
// xess.h / xess_d3d11.h (XeSS SDK 1.3+, MIT) exactly. Execution is REAL:
// xessD3D11CreateContext -> xessD3D11Init -> xessD3D11Execute with color,
// depth and motion-vector textures feeding the network.
//
// XeSS D3D11 officially supports Intel Arc and later. On other vendors the
// Init/Execute fails and the backend honestly reports unavailable so the
// pipeline falls back to the FSR spatial path.

#pragma once

#if defined(_WIN32)

#include <windows.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include "../IReconstructionBackend.h"
#include "../../../core/resources/GpuTexture.h"
#include "../../../core/frame/Resolution.h"

namespace omnirender::backends::xess {

class XessReconstructionBackend final : public IReconstructionBackend {
public:
    XessReconstructionBackend() = default;
    ~XessReconstructionBackend() override;

    XessReconstructionBackend(const XessReconstructionBackend&) = delete;
    XessReconstructionBackend& operator=(const XessReconstructionBackend&) = delete;

    std::string_view GetName() const noexcept override { return "XeSS-Neural"; }
    bool IsRuntimeAvailable() const noexcept override { return runtime_available_; }

    bool Initialize(graphics::IGraphicsDevice& device,
                    const core::Resolution& in_res,
                    const core::Resolution& out_res) override;
    void Shutdown() override;
    void OnDeviceLost() override;
    bool OnDeviceRestored(graphics::IGraphicsDevice& device) override;

    ReconstructionResult Execute(core::FrameContext& fc,
                                 graphics::ICommandContext& ctx) override;

    [[nodiscard]] const std::string& LastErrorString() const noexcept { return last_error_; }

private:
    // ---- Mirrored XeSS ABI (packed(8), matches xess.h / xess_d3d11.h) ----
    struct Xess2D { uint32_t x; uint32_t y; };
    using XessCoord = Xess2D;

    struct alignas(8) XessD3D11InitParams {
        Xess2D   output_resolution;
        int32_t  quality_setting;
        uint32_t init_flags;
    };

    struct alignas(8) XessD3D11ExecuteParams {
        ID3D11Resource* pColorTexture;
        ID3D11Resource* pVelocityTexture;
        ID3D11Resource* pDepthTexture;
        ID3D11Resource* pExposureScaleTexture;
        ID3D11Resource* pResponsivePixelMaskTexture;
        ID3D11Resource* pOutputTexture;
        float    jitter_offset_x;
        float    jitter_offset_y;
        float    exposure_scale;
        uint32_t reset_history;
        uint32_t input_width;
        uint32_t input_height;
        XessCoord input_color_base;
        XessCoord input_motion_vector_base;
        XessCoord input_depth_base;
        XessCoord input_responsive_mask_base;
        XessCoord reserved0;
        XessCoord output_color_base;
    };

    bool LoadLibraryAndProbe();
    void ReleaseFeature();
    bool EnsureFeature(uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h);
    bool EnsureOutputTexture();

    graphics::IGraphicsDevice* device_ = nullptr;
    core::Resolution input_res_{};
    core::Resolution output_res_{};
    core::GpuTexture output_texture_{};

    HMODULE module_ = nullptr;
    void*   context_ = nullptr;
    // Function pointers resolved by name.
    int (*pfn_create_)(ID3D11Device*, void**) = nullptr;
    int (*pfn_init_)(void*, const void*) = nullptr;
    int (*pfn_execute_)(void*, const void*) = nullptr;
    int (*pfn_destroy_)(void*) = nullptr;

    // Resolution pair the current XeSS feature was initialized for.
    uint32_t feat_in_w_ = 0, feat_in_h_ = 0;
    uint32_t feat_out_w_ = 0, feat_out_h_ = 0;
    bool     feature_ready_ = false;
    bool     runtime_available_ = false;
    std::string last_error_;
};

}  // namespace omnirender::backends::xess

#endif  // _WIN32
