// filepath: backends/reconstruction/dlss/DlssReconstructionBackend.cpp
// Official NVIDIA DLSS Super Resolution backend implementation for OmniRender IReconstructionBackend.

#include "DlssReconstructionBackend.h"
#include <dxgi.h>
#include <shlobj.h>
#include "../../../graphics/abstraction/IGraphicsDevice.h"
#include "../../../graphics/abstraction/ICommandContext.h"
#include "../../../graphics/abstraction/IGraphicsTexture.h"

namespace omnirender::backends::dlss {

std::wstring DlssReconstructionBackend::GetOrCreateNgxDataPath() {
    wchar_t local_app[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH) == 0) {
        GetTempPathW(MAX_PATH, local_app);
        return std::wstring(local_app);
    }
    std::wstring path = std::wstring(local_app) + L"\\OmniRender";
    ::CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\NGX";
    ::CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

DlssReconstructionBackend::DlssReconstructionBackend() = default;

DlssReconstructionBackend::~DlssReconstructionBackend() {
    Shutdown();
}

bool DlssReconstructionBackend::DetectNvidiaHardware(ID3D11Device* device) {
    if (!device) return false;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_dev;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi_dev.GetAddressOf())))) return false;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_dev->GetAdapter(adapter.GetAddressOf()))) return false;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) return false;
    is_nvidia_gpu_ = (desc.VendorId == 0x10DE);
    return is_nvidia_gpu_;
}

void DlssReconstructionBackend::ReleaseFeature() {
    const auto& d = loader_.GetDispatch();
    if (d.pfn_release_feature && ngx_feature_handle_) {
        d.pfn_release_feature(ngx_feature_handle_);
        ngx_feature_handle_ = nullptr;
    }
    if (d.pfn_destroy_params && ngx_params_) {
        d.pfn_destroy_params(ngx_params_);
        ngx_params_ = nullptr;
    }
    if (d.pfn_destroy_params && ngx_cap_params_) {
        d.pfn_destroy_params(ngx_cap_params_);
        ngx_cap_params_ = nullptr;
    }
}

bool DlssReconstructionBackend::EnsureOutputTexture(graphics::IGraphicsDevice& device) {
    if (output_texture_.IsValid() &&
        output_texture_.GetWidth() == configured_output_res_.width &&
        output_texture_.GetHeight() == configured_output_res_.height) {
        return true;
    }
    core::TextureDesc desc{
        configured_output_res_.width, configured_output_res_.height, 1,
        core::TextureFormat::R8G8B8A8_UNORM,
        core::TextureUsage::ShaderResource | core::TextureUsage::RenderTarget |
            core::TextureUsage::UnorderedAccess | core::TextureUsage::TransferDst | core::TextureUsage::TransferSrc,
        "DLSS_Reconstructed_Output"
    };
    auto tex = device.CreateTexture(desc);
    if (!tex) return false;
    output_texture_ = core::GpuTexture(std::move(tex));
    return output_texture_.IsValid();
}

bool DlssReconstructionBackend::CreateNgxFeature(ID3D11DeviceContext* imm_ctx) {
    const auto& d = loader_.GetDispatch();
    if (!d.pfn_alloc_params || !d.pfn_create_feature || !imm_ctx) return false;
    if (NVSDK_NGX_FAILED(d.pfn_alloc_params(&ngx_params_)) || !ngx_params_) return false;

    int create_flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    ngx_params_->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, create_flags);
    ngx_params_->Set(NVSDK_NGX_Parameter_Width, configured_input_res_.width);
    ngx_params_->Set(NVSDK_NGX_Parameter_Height, configured_input_res_.height);
    ngx_params_->Set(NVSDK_NGX_Parameter_OutWidth, configured_output_res_.width);
    ngx_params_->Set(NVSDK_NGX_Parameter_OutHeight, configured_output_res_.height);
    ngx_params_->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
    ngx_params_->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1);
    ngx_params_->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1);

    NVSDK_NGX_Result rc = d.pfn_create_feature(imm_ctx, NVSDK_NGX_Feature_SuperSampling, ngx_params_, &ngx_feature_handle_);
    return NVSDK_NGX_SUCCEED(rc) && ngx_feature_handle_ != nullptr;
}

bool DlssReconstructionBackend::Initialize(graphics::IGraphicsDevice& device,
                                           const core::Resolution& in_res,
                                           const core::Resolution& out_res) {
    if (initialized_) Shutdown();

    device_ = &device;
    configured_input_res_ = in_res;
    configured_output_res_ = out_res;
    d3d_device_ = static_cast<ID3D11Device*>(device.GetNativeDevice());
    if (!d3d_device_) { state_ = DlssState::Uninitialized; return false; }

    auto fail = [&](DlssState s) { Shutdown(); state_ = s; return false; };

    if (!DetectNvidiaHardware(d3d_device_)) { state_ = DlssState::NonNvidiaGpu; return false; }
    if (!loader_.LoadLibraries()) { state_ = DlssState::DllNotFound; return false; }
    state_ = DlssState::SdkLoaded;

    if (!EnsureOutputTexture(device)) return fail(DlssState::EvaluationFailed);

    std::wstring ngx_dir = GetOrCreateNgxDataPath();
    const auto& d = loader_.GetDispatch();
    NVSDK_NGX_Result init_rc = d.pfn_init(NVSDK_NGX_APP_ID, ngx_dir.c_str(), d3d_device_, nullptr, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(init_rc)) return fail(DlssState::SdkLoaded);
    state_ = DlssState::ContextCreated;

    if (d.pfn_get_cap_params && NVSDK_NGX_SUCCEED(d.pfn_get_cap_params(&ngx_cap_params_)) && ngx_cap_params_) {
        int sr_avail = 1;
        if (NVSDK_NGX_SUCCEED(ngx_cap_params_->Get(NVSDK_NGX_Parameter_SuperResolution_Available, &sr_avail)) && sr_avail == 0) {
            return fail(DlssState::CapabilityDenied);
        }
    }

    auto imm_cmd = device.GetImmediateContext();
    auto* imm_ctx = imm_cmd ? static_cast<ID3D11DeviceContext*>(imm_cmd->GetNativeContext()) : nullptr;
    if (!CreateNgxFeature(imm_ctx)) return fail(DlssState::ContextCreated);

    state_ = DlssState::FeatureCreated;
    is_runtime_available_ = true;
    initialized_ = true;
    return true;
}

bool DlssReconstructionBackend::Resize(const core::Resolution& in_res, const core::Resolution& out_res) {
    if (configured_input_res_ == in_res && configured_output_res_ == out_res && ngx_feature_handle_) return true;
    configured_input_res_ = in_res;
    configured_output_res_ = out_res;
    ReleaseFeature();
    if (!device_ || !EnsureOutputTexture(*device_)) return false;
    auto imm_cmd = device_->GetImmediateContext();
    auto* imm_ctx = imm_cmd ? static_cast<ID3D11DeviceContext*>(imm_cmd->GetNativeContext()) : nullptr;
    if (!CreateNgxFeature(imm_ctx)) {
        state_ = DlssState::ContextCreated;
        is_runtime_available_ = false;
        return false;
    }
    state_ = DlssState::FeatureCreated;
    is_runtime_available_ = true;
    return true;
}

void DlssReconstructionBackend::OnDeviceLost() {
    ReleaseFeature();
    const auto& d = loader_.GetDispatch();
    if (d.pfn_shutdown && d3d_device_) {
        d.pfn_shutdown();
    }
    output_texture_.Reset();
    d3d_device_ = nullptr;
    is_runtime_available_ = false;
    initialized_ = false;
    state_ = DlssState::DeviceLost;
}

bool DlssReconstructionBackend::OnDeviceRestored(graphics::IGraphicsDevice& device) {
    return Initialize(device, configured_input_res_, configured_output_res_);
}

ReconstructionResult DlssReconstructionBackend::Execute(core::FrameContext& fc, graphics::ICommandContext& cmd_ctx) {
    if (!initialized_ || !is_runtime_available_ || !ngx_feature_handle_ || !fc.color.IsValid()) {
        return { {}, {}, false };
    }
    uint32_t in_w = fc.input_resolution.width > 0 ? fc.input_resolution.width : configured_input_res_.width;
    uint32_t in_h = fc.input_resolution.height > 0 ? fc.input_resolution.height : configured_input_res_.height;
    uint32_t out_w = fc.output_resolution.width > 0 ? fc.output_resolution.width : configured_output_res_.width;
    uint32_t out_h = fc.output_resolution.height > 0 ? fc.output_resolution.height : configured_output_res_.height;
    if (in_w != configured_input_res_.width || in_h != configured_input_res_.height ||
        out_w != configured_output_res_.width || out_h != configured_output_res_.height) {
        if (!Resize({ in_w, in_h }, { out_w, out_h })) return { {}, {}, false };
    }
    if (!EnsureOutputTexture(*device_)) return { {}, {}, false };

    auto* in_color = static_cast<ID3D11Resource*>(fc.color.Get()->GetNativeResource());
    auto* out_color = static_cast<ID3D11Resource*>(output_texture_.Get()->GetNativeResource());
    if (!in_color || !out_color) return { {}, {}, false };

    auto* depth = fc.depth.IsValid() ? static_cast<ID3D11Resource*>(fc.depth.Get()->GetNativeResource()) : nullptr;
    auto* motion = fc.motion.IsValid() ? static_cast<ID3D11Resource*>(fc.motion.Get()->GetNativeResource()) : nullptr;
    auto* reactive = fc.reactive.IsValid() ? static_cast<ID3D11Resource*>(fc.reactive.Get()->GetNativeResource()) : nullptr;

    ngx_params_->Set(NVSDK_NGX_Parameter_Color, in_color);
    ngx_params_->Set(NVSDK_NGX_Parameter_Output, out_color);
    if (depth) ngx_params_->Set(NVSDK_NGX_Parameter_Depth, depth);
    if (motion) ngx_params_->Set(NVSDK_NGX_Parameter_MotionVectors, motion);
    if (reactive) ngx_params_->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, reactive);

    float dir_sign = (mv_convention_.direction == MotionVectorDirection::PreviousToCurrent) ? -1.0f : 1.0f;
    float y_sign = mv_convention_.y_inverted ? -1.0f : 1.0f;
    float mv_scale_x = dir_sign;
    float mv_scale_y = dir_sign * y_sign;
    if (mv_convention_.format == MotionVectorFormat::NormalizedNDC) {
        mv_scale_x *= static_cast<float>(in_w) * 0.5f;
        mv_scale_y *= static_cast<float>(in_h) * 0.5f;
    } else if (mv_convention_.format == MotionVectorFormat::NormalizedUV) {
        mv_scale_x *= static_cast<float>(in_w);
        mv_scale_y *= static_cast<float>(in_h);
    }
    ngx_params_->Set(NVSDK_NGX_Parameter_MV_Scale_X, mv_scale_x);
    ngx_params_->Set(NVSDK_NGX_Parameter_MV_Scale_Y, mv_scale_y);
    ngx_params_->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, fc.jitter.jitter_x);
    ngx_params_->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, fc.jitter.jitter_y);
    ngx_params_->Set(NVSDK_NGX_Parameter_PreExposure, 1.0f);
    ngx_params_->Set(NVSDK_NGX_Parameter_Reset, !fc.validity.history_valid ? 1 : 0);
    ngx_params_->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    ngx_params_->Set(NVSDK_NGX_Parameter_Width, in_w);
    ngx_params_->Set(NVSDK_NGX_Parameter_Height, in_h);
    ngx_params_->Set(NVSDK_NGX_Parameter_OutWidth, out_w);
    ngx_params_->Set(NVSDK_NGX_Parameter_OutHeight, out_h);
    ngx_params_->Set(NVSDK_NGX_Parameter_DLSS_Subrect_Width, in_w);
    ngx_params_->Set(NVSDK_NGX_Parameter_DLSS_Subrect_Height, in_h);
    ngx_params_->Set(NVSDK_NGX_Parameter_CameraNear, fc.camera.near_z);
    ngx_params_->Set(NVSDK_NGX_Parameter_CameraFar, fc.camera.far_z);
    ngx_params_->Set(NVSDK_NGX_Parameter_CameraReversedZ, fc.camera.is_reverse_z ? 1 : 0);
    state_ = DlssState::ParametersBound;

    auto* native_ctx = static_cast<ID3D11DeviceContext*>(cmd_ctx.GetNativeContext());
    if (!native_ctx) { state_ = DlssState::EvaluationFailed; return { {}, {}, false }; }

    const auto& d = loader_.GetDispatch();
    NVSDK_NGX_Result eval_rc = d.pfn_eval_feature(native_ctx, ngx_feature_handle_, ngx_params_, nullptr);
    if (NVSDK_NGX_SUCCEED(eval_rc)) {
        state_ = DlssState::EvaluationSucceeded;
        fc.color = output_texture_;
        fc.output = output_texture_;
        fc.output_resolution = configured_output_res_;
        fc.validity.color_valid = true;
        return { output_texture_, configured_output_res_, true };
    }
    state_ = DlssState::EvaluationFailed;
    return { {}, {}, false };
}

void DlssReconstructionBackend::Shutdown() {
    ReleaseFeature();
    const auto& d = loader_.GetDispatch();
    if (d.pfn_shutdown && d3d_device_) d.pfn_shutdown();
    loader_.Unload();
    output_texture_.Reset();
    device_ = nullptr;
    d3d_device_ = nullptr;
    is_runtime_available_ = false;
    is_nvidia_gpu_ = false;
    initialized_ = false;
    state_ = DlssState::Uninitialized;
}

}  // namespace omnirender::backends::dlss
