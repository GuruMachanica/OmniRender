// filepath: backends/reconstruction/dlss/DlssReconstructionBackend.h
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <d3d11.h>
#include <wrl/client.h>
#include "../IReconstructionBackend.h"
#include "DlssLoader.h"
#include "../../../core/resources/GpuTexture.h"
#include "../../../core/frame/Resolution.h"

namespace omnirender::backends::dlss {

// Strict lifecycle state taxonomy for vendor NVIDIA NGX DLSS runtime.
enum class DlssState : uint8_t {
    Uninitialized = 0,
    NonNvidiaGpu,
    DllNotFound,
    SdkLoaded,
    ContextCreated,
    CapabilityDenied,
    FeatureCreated,
    ParametersBound,
    EvaluationSucceeded,
    EvaluationFailed,
    DeviceLost
};

inline const char* GetDlssStateString(DlssState state) noexcept {
    switch (state) {
        case DlssState::Uninitialized:       return "UNINITIALIZED";
        case DlssState::NonNvidiaGpu:        return "NON_NVIDIA_GPU";
        case DlssState::DllNotFound:         return "DLL_NOT_FOUND";
        case DlssState::SdkLoaded:           return "SDK_LOADED";
        case DlssState::ContextCreated:      return "CONTEXT_CREATED";
        case DlssState::CapabilityDenied:    return "CAPABILITY_DENIED";
        case DlssState::FeatureCreated:      return "FEATURE_CREATED";
        case DlssState::ParametersBound:     return "PARAMETERS_BOUND";
        case DlssState::EvaluationSucceeded: return "EVALUATION_SUCCEEDED";
        case DlssState::EvaluationFailed:    return "EVALUATION_FAILED";
        case DlssState::DeviceLost:          return "DEVICE_LOST";
        default:                             return "UNKNOWN";
    }
}

enum class MotionVectorFormat : uint8_t {
    PixelSpace = 0,    // Already in input pixel displacement
    NormalizedNDC = 1, // [-1, 1] NDC space (OmniRender default shader output)
    NormalizedUV = 2   // [0, 1] UV space
};

enum class MotionVectorDirection : uint8_t {
    CurrentToPrevious = 0,
    PreviousToCurrent = 1
};

struct MotionVectorConvention {
    MotionVectorFormat    format     = MotionVectorFormat::NormalizedNDC;
    MotionVectorDirection direction  = MotionVectorDirection::CurrentToPrevious;
    bool                  y_inverted = false;
};

// Production adapter implementing real IReconstructionBackend for official NVIDIA DLSS SDK.
class DlssReconstructionBackend final : public IReconstructionBackend {
public:
    DlssReconstructionBackend();
    ~DlssReconstructionBackend() override;

    [[nodiscard]] std::string_view GetName() const noexcept override {
        return "NVIDIA DLSS Super Resolution";
    }

    [[nodiscard]] bool IsRuntimeAvailable() const noexcept override {
        return is_runtime_available_ && (state_ >= DlssState::FeatureCreated);
    }

    bool Initialize(graphics::IGraphicsDevice& device,
                    const core::Resolution& input_res,
                    const core::Resolution& output_res) override;

    ReconstructionResult Execute(core::FrameContext& frame_ctx,
                                 graphics::ICommandContext& cmd_ctx) override;

    void OnDeviceLost() override;
    bool OnDeviceRestored(graphics::IGraphicsDevice& device) override;

    void Shutdown() override;

    bool Resize(const core::Resolution& input_res, const core::Resolution& output_res);

    [[nodiscard]] const core::GpuTexture& GetOutputTexture() const noexcept {
        return output_texture_;
    }

    [[nodiscard]] bool HasRealNgx() const noexcept {
        return loader_.IsLoaded();
    }

    [[nodiscard]] bool IsNvidiaHardware() const noexcept {
        return is_nvidia_gpu_;
    }

    [[nodiscard]] DlssState GetState() const noexcept {
        return state_;
    }

    [[nodiscard]] const char* GetStateString() const noexcept {
        return GetDlssStateString(state_);
    }

    void SetMotionVectorFormat(MotionVectorFormat format) noexcept {
        mv_convention_.format = format;
    }

    [[nodiscard]] MotionVectorFormat GetMotionVectorFormat() const noexcept {
        return mv_convention_.format;
    }

    void SetMotionVectorConvention(const MotionVectorConvention& conv) noexcept {
        mv_convention_ = conv;
    }

    [[nodiscard]] const MotionVectorConvention& GetMotionVectorConvention() const noexcept {
        return mv_convention_;
    }

private:
    bool DetectNvidiaHardware(ID3D11Device* device);
    bool EnsureOutputTexture(graphics::IGraphicsDevice& device);
    bool CreateNgxFeature(ID3D11DeviceContext* imm_ctx);
    void ReleaseFeature();

    static std::wstring GetOrCreateNgxDataPath();

    DlssLoader                 loader_;
    core::Resolution           configured_input_res_{};
    core::Resolution           configured_output_res_{};
    core::GpuTexture           output_texture_{};
    graphics::IGraphicsDevice* device_ = nullptr;
    ID3D11Device*              d3d_device_ = nullptr;

    NVSDK_NGX_Parameter*       ngx_params_ = nullptr;
    NVSDK_NGX_Parameter*       ngx_cap_params_ = nullptr;
    NVSDK_NGX_Handle*          ngx_feature_handle_ = nullptr;

    DlssState                  state_ = DlssState::Uninitialized;
    MotionVectorConvention     mv_convention_{};
    bool                       is_nvidia_gpu_ = false;
    bool                       is_runtime_available_ = false;
    bool                       initialized_ = false;
};

}  // namespace omnirender::backends::dlss
