// filepath: modules/daemon/upscalers/dlss_adapter.h
#pragma once

#include <d3d11.h>
#include <windows.h>
#include <cstdint>
#include "../../common/backend_interfaces.h"
#include "../../common/backend_capabilities.h"
#include "../../common/nvsdk_ngx/nvsdk_ngx.h"

namespace omnirender::daemon::upscaler {

// Strict state taxonomy for vendor DLSS runtime lifecycle.
enum class DlssState : uint8_t {
    DllNotFound = 0,
    GpuUnsupported,
    SdkLoaded,
    ContextCreated,
    FeatureCreated,
    ParametersBound,
    InputInvalid,
    ExecutionFailed,
    ExecutionSucceeded
};

inline const char* GetDlssStateString(DlssState state) noexcept {
    switch (state) {
        case DlssState::DllNotFound:        return "DLL_NOT_FOUND";
        case DlssState::GpuUnsupported:     return "GPU_UNSUPPORTED";
        case DlssState::SdkLoaded:          return "SDK_LOADED";
        case DlssState::ContextCreated:     return "CONTEXT_CREATED";
        case DlssState::FeatureCreated:     return "FEATURE_CREATED";
        case DlssState::ParametersBound:    return "PARAMETERS_BOUND";
        case DlssState::InputInvalid:       return "INPUT_INVALID";
        case DlssState::ExecutionFailed:    return "EXECUTION_FAILED";
        case DlssState::ExecutionSucceeded: return "EXECUTION_SUCCEEDED";
        default:                            return "UNKNOWN";
    }
}

// Structured evaluation parameters extracted from FrameContext for NGX.
struct DlssEvaluationParams {
    ID3D11Resource* in_color = nullptr;
    ID3D11Resource* out_color = nullptr;
    ID3D11Resource* depth = nullptr;
    ID3D11Resource* motion = nullptr;
    ID3D11Resource* exposure = nullptr;
    ID3D11Resource* reactive = nullptr;
    float           jitter_offset_x = 0.0f;
    float           jitter_offset_y = 0.0f;
    float           sharpness = 0.0f;
    bool            reset_history = false;
    uint32_t        render_width = 0;
    uint32_t        render_height = 0;
    uint32_t        target_width = 0;
    uint32_t        target_height = 0;
    float           camera_near = 0.1f;
    float           camera_far = 1000.0f;
    bool            camera_reversed_z = false;
    uint64_t        frame_index = 0;
};

// Production adapter implementing real IReconstructionBackend for NVIDIA DLSS.
class DlssAdapter final : public IReconstructionBackend {
public:
    DlssAdapter();
    ~DlssAdapter() override;

    BackendType GetType() const noexcept override { return BackendType::DLSS; }
    const char* GetName() const noexcept override { return "NVIDIA DLSS Super Resolution"; }
    BackendCapabilities GetCapabilities() const noexcept override;

    BackendId GetId() const noexcept {
        return { VendorId::Nvidia, TechnologyId::DLSS, FeatureId::SuperResolution, 3, 7 };
    }

    RuntimeCapabilities GetRuntimeCapabilities() const noexcept {
        return runtime_caps_;
    }

    DlssState GetState() const noexcept { return state_; }
    const DlssEvaluationParams& GetCurrentParams() const noexcept { return current_params_; }
    ID3D11Texture2D* GetOutputTexture() const noexcept { return output_texture_; }

    bool Initialize(uint32_t max_width, uint32_t max_height) override;
    bool InitializeWithDevice(ID3D11Device* device, uint32_t max_width, uint32_t max_height);
    bool BindFrameContextParameters(const FrameContext& ctx, DlssEvaluationParams& out_params);
    bool Execute(FrameContext& ctx) override;
    void Shutdown() override;

    void OnDeviceLost();
    bool OnDeviceRestored(ID3D11Device* new_device);

    bool IsRuntimeAvailable() const noexcept {
        return (state_ >= DlssState::SdkLoaded && state_ != DlssState::GpuUnsupported);
    }
    bool IsGpuSupported() const noexcept {
        return runtime_caps_.is_gpu_supported;
    }
    bool DetectNvidiaHardware(ID3D11Device* device) noexcept;

private:
    bool LoadNgxLibraries();
    void UnloadLibraries();
    bool EnsureOutputTexture(uint32_t width, uint32_t height);

    ID3D11Device*        device_ = nullptr;
    ID3D11Texture2D*     output_texture_ = nullptr;
    HMODULE              ngx_module_ = nullptr;
    NVSDK_NGX_Parameter* ngx_params_ = nullptr;
    NVSDK_NGX_Handle*    ngx_feature_handle_ = nullptr;
    DlssState            state_ = DlssState::DllNotFound;
    DlssEvaluationParams current_params_{};
    RuntimeCapabilities  runtime_caps_{};
    uint32_t             max_width_ = 0;
    uint32_t             max_height_ = 0;
    bool                 initialized_ = false;
};

DlssAdapter& GetGlobalDlssAdapter();

}  // namespace omnirender::daemon::upscaler
