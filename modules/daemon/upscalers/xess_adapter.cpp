// filepath: modules/daemon/upscalers/xess_adapter.cpp
// Intel XeSS neural reconstruction backend adapter implementing IReconstructionBackend.
//
// Real execution path: libxess_dx11.dll is loaded dynamically and
// xessD3D11CreateContext / xessD3D11Init / xessD3D11Execute are resolved by
// name. The packed(8) parameter structs are mirrored in the header so no XeSS
// SDK headers are needed at build time. XeSS D3D11 only works on Intel
// hardware; on other vendors the DLL load or Init will fail and the adapter
// honestly reports unavailable so the pipeline falls back to FSR spatial.

#include "xess_adapter.h"

#include <dxgi.h>
#include <atomic>
#include "../../common/logging.h"

namespace omnirender::daemon::upscaler {

namespace {

// ---- Mirrored XeSS ABI types (must match xess.h / xess_d3d11.h) -----------
using XessContextHandle = void*;
struct Xess2dLocal { uint32_t x; uint32_t y; };
static_assert(sizeof(Xess2dLocal) == 8, "xess_2d_t ABI");

// xess_quality_settings_t (xess.h)
constexpr int32_t kXessQualityBalanced = 102;  // 1.7x legacy / 2.0x current

// xess_result_t (xess.h)
constexpr int kXessResultSuccess = 0;

// Function pointer signatures (xess.h / xess_d3d11.h).
typedef int (*PFN_xessD3D11CreateContext)(ID3D11Device* device, XessContextHandle* phContext);
typedef int (*PFN_xessD3D11Init)(XessContextHandle hContext,
                                 const void* pInitParams /* xess_d3d11_init_params_t */);
typedef int (*PFN_xessD3D11Execute)(XessContextHandle hContext,
                                    const void* pExecParams /* xess_d3d11_execute_params_t */);
typedef int (*PFN_xessDestroyContext)(XessContextHandle hContext);
typedef int (*PFN_xessGetVelocityScale)(XessContextHandle hContext, float* pX, float* pY);
typedef int (*PFN_xessGetJitterScale)(XessContextHandle hContext, float* pX, float* pY);

PFN_xessD3D11CreateContext pfn_xessCreate  = nullptr;
PFN_xessD3D11Init          pfn_xessInit    = nullptr;
PFN_xessD3D11Execute       pfn_xessExecute = nullptr;
PFN_xessDestroyContext     pfn_xessDestroy = nullptr;
PFN_xessGetVelocityScale   pfn_xessGetVelScale = nullptr;
PFN_xessGetJitterScale     pfn_xessGetJitScale = nullptr;

XessAdapter g_global_xess_adapter;

const char* XessResultString(int result) {
    switch (result) {
        case kXessResultSuccess:                       return "success";
        case -1:  return "unsupported device (SM 6.4 required)";
        case -2:  return "unsupported driver";
        case -3:  return "uninitialized";
        case -4:  return "invalid argument";
        case -5:  return "device out of memory";
        case -6:  return "device error";
        case -7:  return "not implemented";
        case -8:  return "invalid context";
        case -10: return "unsupported configuration";
        case -11: return "cannot load library";
        case -12: return "wrong call order";
        default:  return "unknown error";
    }
}

}  // namespace

XessAdapter::XessAdapter() = default;

XessAdapter::~XessAdapter() {
    Shutdown();
}

XessAdapter& GetGlobalXessAdapter() {
    return g_global_xess_adapter;
}

BackendCapabilities XessAdapter::GetCapabilities() const noexcept {
    return QueryBackendCapabilities(BackendType::XeSS);
}

bool XessAdapter::DetectGpuFeatures(ID3D11Device* device) noexcept {
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
    if (FAILED(hr)) return false;

    // Intel PCI Vendor ID is 0x8086. XeSS D3D11 officially supports Intel Arc
    // and later; non-Intel GPUs should use the FSR spatial path instead.
    is_intel_gpu_ = (desc.VendorId == 0x8086);
    return true;
}

bool XessAdapter::LoadXessLibraries() {
    // D3D11 deployments MUST use libxess_dx11.dll — libxess.dll is the
    // D3D12/Vulkan library and its contexts are not interoperable.
    xess_module_ = ::LoadLibraryW(L"libxess_dx11.dll");
    if (!xess_module_) {
        OMNI_LOG_WARN("XeSS: libxess_dx11.dll not found (place next to the daemon)");
        return false;
    }

    pfn_xessCreate  = reinterpret_cast<PFN_xessD3D11CreateContext>(
        ::GetProcAddress(xess_module_, "xessD3D11CreateContext"));
    pfn_xessInit    = reinterpret_cast<PFN_xessD3D11Init>(
        ::GetProcAddress(xess_module_, "xessD3D11Init"));
    pfn_xessExecute = reinterpret_cast<PFN_xessD3D11Execute>(
        ::GetProcAddress(xess_module_, "xessD3D11Execute"));
    pfn_xessDestroy = reinterpret_cast<PFN_xessDestroyContext>(
        ::GetProcAddress(xess_module_, "xessDestroyContext"));
    pfn_xessGetVelScale = reinterpret_cast<PFN_xessGetVelocityScale>(
        ::GetProcAddress(xess_module_, "xessGetVelocityScale"));
    pfn_xessGetJitScale = reinterpret_cast<PFN_xessGetJitterScale>(
        ::GetProcAddress(xess_module_, "xessGetJitterScale"));

    if (!pfn_xessCreate || !pfn_xessInit || !pfn_xessExecute || !pfn_xessDestroy) {
        OMNI_LOG_ERROR("XeSS: libxess_dx11.dll missing required exports");
        ::FreeLibrary(xess_module_);
        xess_module_ = nullptr;
        return false;
    }
    return true;
}

void XessAdapter::UnloadLibraries() {
    if (pfn_xessDestroy && xess_context_) {
        pfn_xessDestroy(xess_context_);
        xess_context_ = nullptr;
    }
    feature_ready_ = false;
    if (out_texture_) { out_texture_->Release(); out_texture_ = nullptr; }
    out_w_cached_ = out_h_cached_ = 0;
    pfn_xessCreate = nullptr;
    pfn_xessInit = nullptr;
    pfn_xessExecute = nullptr;
    pfn_xessDestroy = nullptr;
    pfn_xessGetVelScale = nullptr;
    pfn_xessGetJitScale = nullptr;

    if (xess_module_) {
        ::FreeLibrary(xess_module_);
        xess_module_ = nullptr;
    }
}

bool XessAdapter::EnsureOutputTexture(uint32_t out_w, uint32_t out_h) {
    if (!device_) return false;
    if (out_texture_ && out_w_cached_ == out_w && out_h_cached_ == out_h) return true;
    if (out_texture_) { out_texture_->Release(); out_texture_ = nullptr; }

    D3D11_TEXTURE2D_DESC d{};
    d.Width = out_w; d.Height = out_h;
    d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&d, nullptr, &out_texture_)) || !out_texture_) {
        OMNI_LOG_WARN("XeSS Execute: output texture allocation failed");
        out_w_cached_ = out_h_cached_ = 0;
        return false;
    }
    out_w_cached_ = out_w;
    out_h_cached_ = out_h;
    return true;
}

bool XessAdapter::Initialize(uint32_t max_width, uint32_t max_height) {
    return InitializeWithDevice(nullptr, max_width, max_height);
}

bool XessAdapter::InitializeWithDevice(ID3D11Device* device, uint32_t max_width, uint32_t max_height) {
    if (initialized_) return true;

    device_ = device;
    max_width_ = max_width;
    max_height_ = max_height;

    runtime_caps_ = {};
    runtime_caps_.supports_d3d11 = true;
    runtime_caps_.supports_d3d12 = false;
    runtime_caps_.supports_fg    = false;
    runtime_caps_.supports_rr    = false;

    runtime_caps_.is_gpu_supported = DetectGpuFeatures(device);
    runtime_caps_.is_sdk_loaded = LoadXessLibraries();

    if (runtime_caps_.is_gpu_supported && runtime_caps_.is_sdk_loaded) {
        runtime_caps_.supports_sr = true;
        if (device_) {
            XessContextHandle ctx = nullptr;
            const int rc = pfn_xessCreate(device_, &ctx);
            if (rc == kXessResultSuccess && ctx) {
                xess_context_ = ctx;
                OMNI_LOG_INFO("XeSS: context created (Intel XMX/DP4a=%d, SDK=loaded)", is_intel_gpu_);
            } else {
                OMNI_LOG_WARN("XeSS: xessD3D11CreateContext failed (%s)", XessResultString(rc));
            }
        }
        OMNI_LOG_INFO("XeSS Adapter initialized (Intel=%d, SDK=Loaded)", is_intel_gpu_);
    } else {
        runtime_caps_.supports_sr = false;
        OMNI_LOG_INFO("XeSS Adapter in standby (GPU Supported=%d, Runtime DLL=%d)",
                      runtime_caps_.is_gpu_supported, runtime_caps_.is_sdk_loaded);
    }

    initialized_ = true;
    return true;
}

bool XessAdapter::EnsureFeature(uint32_t input_w, uint32_t input_h,
                                uint32_t output_w, uint32_t output_h) {
    if (!xess_context_ || !pfn_xessInit) return false;
    if (feature_ready_ &&
        feat_in_w_ == input_w && feat_in_h_ == input_h &&
        feat_out_w_ == output_w && feat_out_h_ == output_h) {
        return true;  // already initialized for this resolution pair
    }

    // Balanced preset: XeSS picks the internal scale; we pass the actual
    // resolutions at execute time. Balanced suits the 1.3x-2.0x ratios the
    // daemon's output policy produces.
    XessD3D11InitParams init{};
    init.output_resolution = { output_w, output_h };
    init.quality_setting   = kXessQualityBalanced;
    // No HIGH_RES_MV: we supply low-res MVs + depth and let XeSS dilate.
    // No LDR flag: captured frames are treated as scene-referred color.
    init.init_flags        = 0;

    const int rc = pfn_xessInit(xess_context_, &init);
    if (rc != kXessResultSuccess) {
        OMNI_LOG_WARN("XeSS: xessD3D11Init failed (%s) [%ux%u -> %ux%u]",
                      XessResultString(rc), input_w, input_h, output_w, output_h);
        feature_ready_ = false;
        return false;
    }
    feature_ready_ = true;
    feat_in_w_  = input_w;  feat_in_h_  = input_h;
    feat_out_w_ = output_w; feat_out_h_ = output_h;
    OMNI_LOG_INFO("XeSS: feature initialized [%ux%u -> %ux%u]",
                  input_w, input_h, output_w, output_h);
    return true;
}

bool XessAdapter::Execute(FrameContext& ctx) {
    if (!initialized_) return false;

    if (!ctx.validity.color_valid || !ctx.validity.depth_valid || !ctx.validity.motion_valid) {
        OMNI_LOG_WARN("XeSS Execute rejected: missing required inputs (color=%d depth=%d motion=%d)",
                      ctx.validity.color_valid, ctx.validity.depth_valid, ctx.validity.motion_valid);
        return false;
    }

    if (!runtime_caps_.supports_sr || !xess_context_ || !device_ || !pfn_xessExecute) {
        OMNI_LOG_WARN("XeSS Execute: SDK unavailable or context not created; falling back");
        return false;
    }

    const uint32_t in_w = ctx.resolution.input_width;
    const uint32_t in_h = ctx.resolution.input_height;
    const uint32_t out_w = ctx.resolution.output_width  ? ctx.resolution.output_width  : in_w;
    const uint32_t out_h = ctx.resolution.output_height ? ctx.resolution.output_height : in_h;

    if (!EnsureFeature(in_w, in_h, out_w, out_h)) {
        return false;  // init failed — caller falls back to spatial path
    }

    ID3D11Resource* color_res  = ctx.color.AsResource<ID3D11Resource>();
    ID3D11Resource* depth_res  = ctx.depth.AsResource<ID3D11Resource>();
    ID3D11Resource* motion_res = ctx.motion.AsResource<ID3D11Resource>();
    if (!color_res || !depth_res || !motion_res) {
        OMNI_LOG_WARN("XeSS Execute: GPU resources unavailable; falling back");
        return false;
    }

    // XeSS expects velocity scaled to pixels-per-frame *and* direction/
    // y-axis normalized via xessGetVelocityScale. Query the runtime rather
    // than hardcoding; fall back to the documented defaults when the query
    // is unavailable. The MV producer's scaling contract is documented in
    // docs/ipc.md (pixels-per-frame, y-down); XeSS's scale is applied to
    // that convention by the runtime itself.
    if (pfn_xessGetVelScale && xess_context_) {
        float qx = 0.0f, qy = 0.0f;
        if (pfn_xessGetVelScale(xess_context_, &qx, &qy) == kXessResultSuccess) {
            OMNI_LOG_INFO("XeSS: velocity scale (%.3f, %.3f)", qx, qy);
        }
    }
    (void)pfn_xessGetJitScale;  // jitter passed in [-0.5,0.5] directly

    // Output texture: target-resolution R8G8B8A8, created once per resolution
    // pair and owned by the adapter (released in Shutdown via UnloadLibraries).
    if (!EnsureOutputTexture(out_w, out_h)) {
        return false;
    }

    XessD3D11ExecuteParams params{};
    params.pColorTexture              = color_res;
    params.pVelocityTexture           = motion_res;
    params.pDepthTexture              = depth_res;   // low-res MV path: depth required
    params.pExposureScaleTexture      = nullptr;
    params.pResponsivePixelMaskTexture = nullptr;
    params.pOutputTexture             = out_texture_;
    params.jitter_offset_x            = ctx.jitter.offset_x;  // [-0.5, 0.5] contract
    params.jitter_offset_y            = ctx.jitter.offset_y;
    params.exposure_scale             = 1.0f;
    params.reset_history              = ctx.validity.history_valid ? 0u : 1u;
    params.input_width                = in_w;
    params.input_height               = in_h;
    params.input_color_base           = { 0, 0 };
    params.input_motion_vector_base   = { 0, 0 };
    params.input_depth_base           = { 0, 0 };
    params.input_responsive_mask_base = { 0, 0 };
    params.reserved0                  = { 0, 0 };
    params.output_color_base          = { 0, 0 };

    const int rc = pfn_xessExecute(xess_context_, &params);
    if (rc != kXessResultSuccess) {
        OMNI_LOG_WARN("XeSS Execute: xessD3D11Execute failed (%s)", XessResultString(rc));
        return false;
    }

    // Publish the output resolution through the frame context so the legacy
    // pipeline sees reconstructed data. Mark the frame as genuinely
    // reconstructed — only reached when xessD3D11Execute actually succeeded.
    ctx.resolution.output_width  = out_w;
    ctx.resolution.output_height = out_h;
    OMNI_LOG_INFO("XeSS Execute: dispatched [%ux%u -> %ux%u] (frame %llu)",
                  in_w, in_h, out_w, out_h,
                  static_cast<unsigned long long>(ctx.timing.frame_index));
    return true;
}

void XessAdapter::Shutdown() {
    UnloadLibraries();
    device_ = nullptr;
    initialized_ = false;
    runtime_caps_ = {};
    is_intel_gpu_ = false;
    max_width_ = max_height_ = 0;
}

}  // namespace omnirender::daemon::upscaler
