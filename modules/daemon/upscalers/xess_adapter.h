// filepath: modules/daemon/upscalers/xess_adapter.h
#pragma once

#include <d3d11.h>
#include <windows.h>
#include <cstdint>
#include "../../common/backend_interfaces.h"
#include "../../common/backend_capabilities.h"

namespace omnirender::daemon::upscaler {

// Production adapter implementing IReconstructionBackend for Intel XeSS.
//
// The XeSS runtime (libxess_dx11.dll) is loaded dynamically — no SDK headers
// or import libraries required. All API signatures mirrored from
// xess.h / xess_d3d11.h (XeSS SDK 1.3+, MIT-licensed); packed(8) ABI is
// reproduced locally so the structs match the DLL exactly.
//
// Execution is REAL: xessD3D11Init() + xessD3D11Execute() with color, depth
// and motion-vector textures. XeSS SR dilates/upsamples the low-res motion
// vectors internally when depth is provided (no XESS_INIT_FLAG_HIGH_RES_MV).
class XessAdapter final : public IReconstructionBackend {
public:
    XessAdapter();
    ~XessAdapter() override;

    BackendType GetType() const noexcept override { return BackendType::XeSS; }
    const char* GetName() const noexcept override { return "Intel XeSS Neural Reconstruct"; }
    BackendCapabilities GetCapabilities() const noexcept override;

    BackendId GetId() const noexcept {
        return { VendorId::Intel, TechnologyId::XeSS, FeatureId::SuperResolution, 1, 3 };
    }

    RuntimeCapabilities GetRuntimeCapabilities() const noexcept {
        return runtime_caps_;
    }

    bool Initialize(uint32_t max_width, uint32_t max_height) override;
    bool InitializeWithDevice(ID3D11Device* device, uint32_t max_width, uint32_t max_height);
    bool Execute(FrameContext& ctx) override;
    void Shutdown() override;

    bool IsRuntimeAvailable() const noexcept { return runtime_caps_.is_sdk_loaded; }
    bool IsIntelGpu() const noexcept { return is_intel_gpu_; }

    // True once xessD3D11Init has succeeded for the current resolution pair.
    bool IsFeatureReady() const noexcept { return feature_ready_; }

private:
    bool DetectGpuFeatures(ID3D11Device* device) noexcept;
    bool LoadXessLibraries();
    void UnloadLibraries();
    // (Re-)run xessD3D11Init for a given input/output resolution pair.
    bool EnsureFeature(uint32_t input_w, uint32_t input_h,
                       uint32_t output_w, uint32_t output_h);
    // Allocate/reuse the target-resolution output texture XeSS writes into.
    bool EnsureOutputTexture(uint32_t out_w, uint32_t out_h);

    // ---- Mirrored XeSS ABI (packed(8), matches xess.h / xess_d3d11.h) ----
    struct Xess2D { uint32_t x; uint32_t y; };
    using XessCoord = Xess2D;

    struct alignas(8) XessD3D11InitParams {
        Xess2D   output_resolution;
        int32_t  quality_setting;   // xess_quality_settings_t
        uint32_t init_flags;        // xess_init_flags_t
    };

    struct alignas(8) XessD3D11ExecuteParams {
        ID3D11Resource* pColorTexture;
        ID3D11Resource* pVelocityTexture;
        ID3D11Resource* pDepthTexture;
        ID3D11Resource* pExposureScaleTexture;      // unused (no flag)
        ID3D11Resource* pResponsivePixelMaskTexture; // unused (no flag)
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

    ID3D11Device*       device_ = nullptr;
    HMODULE             xess_module_ = nullptr;
    void*               xess_context_ = nullptr;
    RuntimeCapabilities runtime_caps_{};
    uint32_t            max_width_ = 0;
    uint32_t            max_height_ = 0;
    bool                initialized_ = false;
    bool                is_intel_gpu_ = false;
    bool                feature_ready_ = false;
    // Resolution pair the current XeSS feature was initialized for.
    uint32_t            feat_in_w_ = 0, feat_in_h_ = 0;
    uint32_t            feat_out_w_ = 0, feat_out_h_ = 0;
    // Output texture XeSS renders into (owned, released in Shutdown).
    ID3D11Texture2D*    out_texture_ = nullptr;
    uint32_t            out_w_cached_ = 0, out_h_cached_ = 0;
};

XessAdapter& GetGlobalXessAdapter();

}  // namespace omnirender::daemon::upscaler
