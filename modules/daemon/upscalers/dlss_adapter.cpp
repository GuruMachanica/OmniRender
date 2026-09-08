// filepath: modules/daemon/upscalers/dlss_adapter.cpp
// Real NVIDIA DLSS Super Resolution execution adapter implementing IReconstructionBackend.

#include "dlss_adapter.h"

#include <dxgi.h>
#include <atomic>
#include "../../common/logging.h"

namespace omnirender::daemon::upscaler {

namespace {

PFN_NVSDK_NGX_D3D11_Init               pfn_NGX_Init           = nullptr;
PFN_NVSDK_NGX_D3D11_Shutdown           pfn_NGX_Shutdown       = nullptr;
PFN_NVSDK_NGX_D3D11_AllocateParameters pfn_NGX_AllocParams     = nullptr;
PFN_NVSDK_NGX_D3D11_DestroyParameters  pfn_NGX_DestroyParams  = nullptr;
PFN_NVSDK_NGX_D3D11_CreateFeature      pfn_NGX_CreateFeature  = nullptr;
PFN_NVSDK_NGX_D3D11_EvaluateFeature    pfn_NGX_EvaluateFeature= nullptr;
PFN_NVSDK_NGX_D3D11_ReleaseFeature     pfn_NGX_ReleaseFeature = nullptr;

DlssAdapter g_global_dlss_adapter;

}  // namespace

DlssAdapter::DlssAdapter() = default;
DlssAdapter::~DlssAdapter() { Shutdown(); }
DlssAdapter& GetGlobalDlssAdapter() { return g_global_dlss_adapter; }

BackendCapabilities DlssAdapter::GetCapabilities() const noexcept {
    return QueryBackendCapabilities(BackendType::DLSS);
}

bool DlssAdapter::DetectNvidiaHardware(ID3D11Device* device) noexcept {
    if (!device) return false;
    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device))) || !dxgi_device) {
        return false;
    }
    IDXGIAdapter* adapter = nullptr;
    HRESULT hr = dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();
    if (FAILED(hr) || !adapter) return false;

    DXGI_ADAPTER_DESC desc{};
    hr = adapter->GetDesc(&desc);
    adapter->Release();
    return SUCCEEDED(hr) && (desc.VendorId == 0x10DE);
}

bool DlssAdapter::LoadNgxLibraries() {
    ngx_module_ = ::LoadLibraryW(L"_nvngx.dll");
    if (!ngx_module_) ngx_module_ = ::LoadLibraryW(L"nvngx.dll");
    if (!ngx_module_) ngx_module_ = ::LoadLibraryW(L"nvngx_dlss.dll");

    if (ngx_module_) {
        pfn_NGX_Init           = reinterpret_cast<PFN_NVSDK_NGX_D3D11_Init>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_Init"));
        pfn_NGX_Shutdown       = reinterpret_cast<PFN_NVSDK_NGX_D3D11_Shutdown>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_Shutdown"));
        pfn_NGX_AllocParams     = reinterpret_cast<PFN_NVSDK_NGX_D3D11_AllocateParameters>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_AllocateParameters"));
        pfn_NGX_DestroyParams  = reinterpret_cast<PFN_NVSDK_NGX_D3D11_DestroyParameters>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_DestroyParameters"));
        pfn_NGX_CreateFeature  = reinterpret_cast<PFN_NVSDK_NGX_D3D11_CreateFeature>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_CreateFeature"));
        pfn_NGX_EvaluateFeature= reinterpret_cast<PFN_NVSDK_NGX_D3D11_EvaluateFeature>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_EvaluateFeature"));
        pfn_NGX_ReleaseFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D11_ReleaseFeature>(::GetProcAddress(ngx_module_, "NVSDK_NGX_D3D11_ReleaseFeature"));
    }
    return (pfn_NGX_Init != nullptr && pfn_NGX_CreateFeature != nullptr && pfn_NGX_EvaluateFeature != nullptr);
}

void DlssAdapter::UnloadLibraries() {
    OnDeviceLost();
    pfn_NGX_Init = nullptr; pfn_NGX_Shutdown = nullptr;
    pfn_NGX_AllocParams = nullptr; pfn_NGX_DestroyParams = nullptr;
    pfn_NGX_CreateFeature = nullptr; pfn_NGX_EvaluateFeature = nullptr;
    pfn_NGX_ReleaseFeature = nullptr;
    if (ngx_module_) { ::FreeLibrary(ngx_module_); ngx_module_ = nullptr; }
}

void DlssAdapter::OnDeviceLost() {
    if (output_texture_) { output_texture_->Release(); output_texture_ = nullptr; }
    if (pfn_NGX_ReleaseFeature && ngx_feature_handle_) {
        pfn_NGX_ReleaseFeature(ngx_feature_handle_);
        ngx_feature_handle_ = nullptr;
    }
    if (pfn_NGX_DestroyParams && ngx_params_) {
        pfn_NGX_DestroyParams(ngx_params_);
        ngx_params_ = nullptr;
    }
    if (pfn_NGX_Shutdown && device_) pfn_NGX_Shutdown();
    device_ = nullptr;
    if (state_ > DlssState::SdkLoaded) state_ = DlssState::SdkLoaded;
}

bool DlssAdapter::OnDeviceRestored(ID3D11Device* new_device) {
    if (!new_device) return false;
    device_ = new_device;
    if (!DetectNvidiaHardware(device_)) {
        state_ = DlssState::GpuUnsupported;
        return false;
    }
    if (pfn_NGX_Init) {
        wchar_t temp_dir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp_dir);
        NVSDK_NGX_Result init_rc = pfn_NGX_Init(NVSDK_NGX_APP_ID, temp_dir, device_, nullptr, NVSDK_NGX_Version_API);
        if (NVSDK_NGX_SUCCEED(init_rc)) {
            state_ = DlssState::ContextCreated;
            if (pfn_NGX_AllocParams) pfn_NGX_AllocParams(&ngx_params_);
            ID3D11DeviceContext* ctx = nullptr;
            device_->GetImmediateContext(&ctx);
            if (ctx && pfn_NGX_CreateFeature) {
                NVSDK_NGX_Result feat_rc = pfn_NGX_CreateFeature(
                    ctx, NVSDK_NGX_Feature_SuperResolution, ngx_params_, &ngx_feature_handle_);
                if (NVSDK_NGX_SUCCEED(feat_rc) && ngx_feature_handle_) {
                    state_ = DlssState::FeatureCreated;
                }
                ctx->Release();
            }
            return (state_ == DlssState::FeatureCreated);
        }
    }
    return false;
}

bool DlssAdapter::EnsureOutputTexture(uint32_t width, uint32_t height) {
    if (!device_ || width == 0 || height == 0) return false;
    if (output_texture_) {
        D3D11_TEXTURE2D_DESC desc{};
        output_texture_->GetDesc(&desc);
        if (desc.Width == width && desc.Height == height) return true;
        output_texture_->Release();
        output_texture_ = nullptr;
    }
    D3D11_TEXTURE2D_DESC desc{ width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 },
                              D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS };
    return SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &output_texture_));
}

bool DlssAdapter::Initialize(uint32_t max_width, uint32_t max_height) {
    return InitializeWithDevice(nullptr, max_width, max_height);
}

bool DlssAdapter::InitializeWithDevice(ID3D11Device* device, uint32_t max_width, uint32_t max_height) {
    if (initialized_) return true;
    device_ = device;
    max_width_ = max_width;
    max_height_ = max_height;
    runtime_caps_ = {};
    runtime_caps_.supports_d3d11 = true;

    if (device && !DetectNvidiaHardware(device)) {
        state_ = DlssState::GpuUnsupported;
        runtime_caps_.is_gpu_supported = false;
        OMNI_LOG_INFO("DLSS Adapter state: %s", GetDlssStateString(state_));
        initialized_ = true;
        return true;
    }

    runtime_caps_.is_gpu_supported = (device != nullptr);
    runtime_caps_.is_sdk_loaded = LoadNgxLibraries();
    if (!runtime_caps_.is_sdk_loaded) {
        state_ = DlssState::DllNotFound;
        OMNI_LOG_INFO("DLSS Adapter state: %s", GetDlssStateString(state_));
        initialized_ = true;
        return true;
    }

    state_ = DlssState::SdkLoaded;
    runtime_caps_.supports_sr = true;

    if (device_) OnDeviceRestored(device_);
    OMNI_LOG_INFO("DLSS Adapter initialized (State: %s)", GetDlssStateString(state_));
    initialized_ = true;
    return true;
}

bool DlssAdapter::BindFrameContextParameters(const FrameContext& ctx, DlssEvaluationParams& out_params) {
    out_params = {};
    if (!ctx.validity.color_valid || !ctx.validity.depth_valid || !ctx.validity.motion_valid) {
        state_ = DlssState::InputInvalid;
        return false;
    }

    out_params.in_color = ctx.color.AsResource<ID3D11Resource>();
    out_params.depth = ctx.depth.AsResource<ID3D11Resource>();
    out_params.motion = ctx.motion.AsResource<ID3D11Resource>();
    if (!out_params.in_color || !out_params.depth || !out_params.motion) {
        state_ = DlssState::InputInvalid;
        return false;
    }

    if (ctx.reactive.is_valid) out_params.reactive = ctx.reactive.AsResource<ID3D11Resource>();
    out_params.jitter_offset_x = ctx.jitter.offset_x;
    out_params.jitter_offset_y = ctx.jitter.offset_y;
    out_params.reset_history = !ctx.validity.history_valid;

    out_params.render_width = ctx.resolution.input_width > 0 ? ctx.resolution.input_width : ctx.color.width;
    out_params.render_height = ctx.resolution.input_height > 0 ? ctx.resolution.input_height : ctx.color.height;
    out_params.target_width = ctx.resolution.output_width > 0 ? ctx.resolution.output_width :
                              (max_width_ > 0 ? max_width_ : out_params.render_width);
    out_params.target_height = ctx.resolution.output_height > 0 ? ctx.resolution.output_height :
                               (max_height_ > 0 ? max_height_ : out_params.render_height);

    if (out_params.render_width == 0 || out_params.render_height == 0) {
        state_ = DlssState::InputInvalid;
        return false;
    }

    if (device_) EnsureOutputTexture(out_params.target_width, out_params.target_height);
    out_params.out_color = output_texture_ ? output_texture_ : out_params.in_color;
    out_params.camera_near = ctx.camera.camera_near;
    out_params.camera_far = ctx.camera.camera_far;
    out_params.camera_reversed_z = ctx.camera.is_reversed_z;
    out_params.frame_index = ctx.timing.frame_index;

    state_ = DlssState::ParametersBound;
    return true;
}

bool DlssAdapter::Execute(FrameContext& ctx) {
    if (!initialized_) return false;

    if (!BindFrameContextParameters(ctx, current_params_)) {
        OMNI_LOG_WARN("DLSS execution rejected: %s", GetDlssStateString(state_));
        return false;
    }

    if (!ngx_feature_handle_ || !pfn_NGX_EvaluateFeature) {
        state_ = DlssState::ExecutionFailed;
        OMNI_LOG_INFO("DLSS evaluation bypassed: runtime feature not created");
        return false;
    }

    if (ngx_params_) {
        ngx_params_->Set(NVSDK_NGX_Parameter_Color, current_params_.in_color);
        ngx_params_->Set(NVSDK_NGX_Parameter_Output, current_params_.out_color);
        ngx_params_->Set(NVSDK_NGX_Parameter_Depth, current_params_.depth);
        ngx_params_->Set(NVSDK_NGX_Parameter_MotionVectors, current_params_.motion);
        if (current_params_.reactive) {
            ngx_params_->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, current_params_.reactive);
        }
        ngx_params_->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, current_params_.jitter_offset_x);
        ngx_params_->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, current_params_.jitter_offset_y);
        ngx_params_->Set(NVSDK_NGX_Parameter_Sharpness, current_params_.sharpness);
        ngx_params_->Set(NVSDK_NGX_Parameter_Reset, current_params_.reset_history ? 1 : 0);
        ngx_params_->Set(NVSDK_NGX_Parameter_Width, current_params_.render_width);
        ngx_params_->Set(NVSDK_NGX_Parameter_Height, current_params_.render_height);
        ngx_params_->Set(NVSDK_NGX_Parameter_OutWidth, current_params_.target_width);
        ngx_params_->Set(NVSDK_NGX_Parameter_OutHeight, current_params_.target_height);
        ngx_params_->Set(NVSDK_NGX_Parameter_CameraNear, current_params_.camera_near);
        ngx_params_->Set(NVSDK_NGX_Parameter_CameraFar, current_params_.camera_far);
        ngx_params_->Set(NVSDK_NGX_Parameter_CameraReversedZ, current_params_.camera_reversed_z ? 1 : 0);
    }

    ID3D11DeviceContext* dev_ctx = nullptr;
    if (device_) device_->GetImmediateContext(&dev_ctx);

    NVSDK_NGX_Result eval_rc = NVSDK_NGX_Result_Fail;
    if (dev_ctx) {
        eval_rc = pfn_NGX_EvaluateFeature(dev_ctx, ngx_feature_handle_, ngx_params_, nullptr);
        dev_ctx->Release();
    }

    if (NVSDK_NGX_SUCCEED(eval_rc)) {
        state_ = DlssState::ExecutionSucceeded;
        if (output_texture_) {
            ctx.color.resource = output_texture_;
            ctx.color.width = current_params_.target_width;
            ctx.color.height = current_params_.target_height;
        }
        ctx.color.source = DataSource::Reconstructed;
        return true;
    } else {
        state_ = DlssState::ExecutionFailed;
        OMNI_LOG_WARN("DLSS evaluation failed: %s (rc=%d)", GetDlssStateString(state_), eval_rc);
        return false;
    }
}

void DlssAdapter::Shutdown() {
    UnloadLibraries();
    initialized_ = false;
    state_ = DlssState::DllNotFound;
    runtime_caps_ = {};
}

}  // namespace omnirender::daemon::upscaler
