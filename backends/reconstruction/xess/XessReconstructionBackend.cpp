// filepath: backends/reconstruction/xess/XessReconstructionBackend.cpp
// Intel XeSS neural reconstruction backend for the core runtime pipeline.
//
// Real execution: libxess_dx11.dll resolved by name at runtime — xessD3D11CreateContext,
// xessD3D11Init, xessD3D11Execute. The packed(8) parameter structs mirror
// xess.h / xess_d3d11.h (XeSS SDK 1.3+, MIT) exactly, so no SDK headers are
// needed at build time. Inputs are the runtime pipeline's linearized depth and
// pixels-per-frame motion vectors; XeSS dilates the MVs itself on the low-res
// MV path.
//
// XeSS D3D11 officially supports Intel Arc and later ONLY — per Intel's SR
// guide, context creation on non-Intel devices fails with
// XESS_RESULT_ERROR_UNSUPPORTED_DEVICE (the cross-vendor DP4a path is D3D12
// only). When the DLL is missing or Init/Execute fail, the backend reports
// unavailable and the pipeline falls back to the FSR spatial path.

#include "XessReconstructionBackend.h"

#include "../../../graphics/abstraction/IGraphicsDevice.h"
#include "../../../graphics/abstraction/ICommandContext.h"
#include "../../../graphics/abstraction/IGraphicsTexture.h"

#if defined(_WIN32)

namespace omnirender::backends::xess {

namespace {

// xess_quality_settings_t (xess.h) — Balanced targets ~1.7x-2.0x, which covers
// the daemon's output-scale policy (screen/quality/ultra).
constexpr int32_t kXessQualityBalanced = 102;

// xess_result_t (xess.h)
constexpr int kXessResultSuccess = 0;

// xess_init_flags (xess.h):
//  - LDR_INPUT_COLOR: the hook captures final (tonemapped) UNORM backbuffer
//    data, which is LDR by definition. Intel's guide requires the flag for
//    LDR input and recommends exposure = 1.0 with no auto-exposure (we pass
//    exposure_scale = 1.0 in Execute).
//  - No HIGH_RES_MV: we supply low-res MVs + depth and let XeSS dilate.
//  - No INVERTED_DEPTH: DepthProvider linearizes to near=0 (smaller = closer,
//    XeSS's default convention) regardless of the game's raw depth order.
constexpr uint32_t kXessInitFlags = 1u << 6;  // XESS_INIT_FLAG_LDR_INPUT_COLOR

const char* XessResultString(int result) {
    switch (result) {
        case kXessResultSuccess: return "success";
        case -1:  return "unsupported device (SM 6.4+ required)";
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

XessReconstructionBackend::~XessReconstructionBackend() {
    Shutdown();
}

bool XessReconstructionBackend::LoadLibraryAndProbe() {
    if (module_) return true;

    // D3D11 deployments MUST use libxess_dx11.dll — libxess.dll is the
    // D3D12/Vulkan library and its contexts are not interoperable.
    module_ = ::LoadLibraryW(L"libxess_dx11.dll");
    if (!module_) {
        last_error_ = "libxess_dx11.dll not found (place next to the daemon executable)";
        return false;
    }

    pfn_create_ = reinterpret_cast<int (*)(ID3D11Device*, void**)>(
        ::GetProcAddress(module_, "xessD3D11CreateContext"));
    pfn_init_ = reinterpret_cast<int (*)(void*, const void*)>(
        ::GetProcAddress(module_, "xessD3D11Init"));
    pfn_execute_ = reinterpret_cast<int (*)(void*, const void*)>(
        ::GetProcAddress(module_, "xessD3D11Execute"));
    pfn_destroy_ = reinterpret_cast<int (*)(void*)>(
        ::GetProcAddress(module_, "xessDestroyContext"));

    if (!pfn_create_ || !pfn_init_ || !pfn_execute_ || !pfn_destroy_) {
        last_error_ = "libxess_dx11.dll missing required exports";
        ::FreeLibrary(module_);
        module_ = nullptr;
        return false;
    }
    return true;
}

void XessReconstructionBackend::ReleaseFeature() {
    if (context_ && pfn_destroy_) {
        pfn_destroy_(context_);
    }
    context_ = nullptr;
    feature_ready_ = false;
    feat_in_w_ = feat_in_h_ = feat_out_w_ = feat_out_h_ = 0;
}

bool XessReconstructionBackend::EnsureFeature(uint32_t in_w, uint32_t in_h,
                                              uint32_t out_w, uint32_t out_h) {
    if (!context_ || !pfn_init_) return false;
    if (feature_ready_ &&
        feat_in_w_ == in_w && feat_in_h_ == in_h &&
        feat_out_w_ == out_w && feat_out_h_ == out_h) {
        return true;  // feature already initialized for this resolution pair
    }

    // XeSS selects its internal network from the output resolution + quality
    // setting; input dimensions are supplied per-execute.
    XessD3D11InitParams init{};
    init.output_resolution = { out_w, out_h };
    init.quality_setting   = kXessQualityBalanced;
    init.init_flags        = kXessInitFlags;

    const int rc = pfn_init_(context_, &init);
    if (rc != kXessResultSuccess) {
        last_error_ = "xessD3D11Init failed: ";
        last_error_ += XessResultString(rc);
        feature_ready_ = false;
        return false;
    }
    feature_ready_ = true;
    feat_in_w_  = in_w;   feat_in_h_  = in_h;
    feat_out_w_ = out_w;  feat_out_h_ = out_h;
    return true;
}

bool XessReconstructionBackend::EnsureOutputTexture() {
    if (!device_) return false;
    if (output_texture_.IsValid() &&
        output_texture_.GetWidth() == output_res_.width &&
        output_texture_.GetHeight() == output_res_.height) {
        return true;
    }
    core::TextureDesc desc{
        output_res_.width, output_res_.height, 1,
        core::TextureFormat::R8G8B8A8_UNORM,
        core::TextureUsage::ShaderResource | core::TextureUsage::RenderTarget |
            core::TextureUsage::UnorderedAccess | core::TextureUsage::TransferDst | core::TextureUsage::TransferSrc,
        "XeSS_Reconstructed_Output"
    };
    auto tex = device_->CreateTexture(desc);
    if (!tex) {
        last_error_ = "output texture allocation failed";
        return false;
    }
    output_texture_ = core::GpuTexture(std::move(tex));
    return output_texture_.IsValid();
}

bool XessReconstructionBackend::Initialize(graphics::IGraphicsDevice& device,
                                           const core::Resolution& in_res,
                                           const core::Resolution& out_res) {
    if (context_) Shutdown();

    device_ = &device;
    input_res_ = in_res;
    output_res_ = out_res;

    ID3D11Device* d3d_device = static_cast<ID3D11Device*>(device.GetNativeDevice());
    if (!d3d_device) {
        last_error_ = "device is not a D3D11 device";
        return false;
    }

    if (!LoadLibraryAndProbe()) return false;

    void* ctx = nullptr;
    const int rc = pfn_create_(d3d_device, &ctx);
    if (rc != kXessResultSuccess || !ctx) {
        last_error_ = "xessD3D11CreateContext failed: ";
        last_error_ += XessResultString(rc);
        ReleaseFeature();
        return false;
    }
    context_ = ctx;

    if (!EnsureOutputTexture()) {
        ReleaseFeature();
        return false;
    }
    if (!EnsureFeature(in_res.width, in_res.height, out_res.width, out_res.height)) {
        ReleaseFeature();
        return false;
    }

    runtime_available_ = true;
    last_error_.clear();
    return true;
}

void XessReconstructionBackend::Shutdown() {
    ReleaseFeature();
    if (module_) {
        ::FreeLibrary(module_);
        module_ = nullptr;
    }
    pfn_create_ = nullptr;
    pfn_init_ = nullptr;
    pfn_execute_ = nullptr;
    pfn_destroy_ = nullptr;
    output_texture_.Reset();
    device_ = nullptr;
    runtime_available_ = false;
}

void XessReconstructionBackend::OnDeviceLost() {
    ReleaseFeature();
    output_texture_.Reset();
    device_ = nullptr;
    runtime_available_ = false;
}

bool XessReconstructionBackend::OnDeviceRestored(graphics::IGraphicsDevice& device) {
    return Initialize(device, input_res_, output_res_);
}

ReconstructionResult XessReconstructionBackend::Execute(core::FrameContext& fc,
                                                        graphics::ICommandContext& cmd_ctx) {
    if (!runtime_available_ || !context_ || !pfn_execute_ || !fc.color.IsValid()) {
        return { {}, {}, false };
    }

    // Resolve per-frame resolutions (they can change after a game resize).
    const uint32_t in_w = fc.input_resolution.width  ? fc.input_resolution.width  : input_res_.width;
    const uint32_t in_h = fc.input_resolution.height ? fc.input_resolution.height : input_res_.height;
    const uint32_t out_w = fc.output_resolution.width  ? fc.output_resolution.width  : output_res_.width;
    const uint32_t out_h = fc.output_resolution.height ? fc.output_resolution.height : output_res_.height;
    if (in_w == 0 || in_h == 0 || out_w == 0 || out_h == 0) {
        return { {}, {}, false };
    }
    if (in_w != input_res_.width || in_h != input_res_.height ||
        out_w != output_res_.width || out_h != output_res_.height) {
        input_res_ = { in_w, in_h };
        output_res_ = { out_w, out_h };
        if (!EnsureOutputTexture() ||
            !EnsureFeature(in_w, in_h, out_w, out_h)) {
            runtime_available_ = false;
            return { {}, {}, false };
        }
    }

    auto* color_res = static_cast<ID3D11Resource*>(fc.color.Get()->GetNativeResource());
    if (!color_res) return { {}, {}, false };

    // XeSS requires depth + velocity on the low-res MV path. The runtime
    // pipeline's temporal passes guarantee both before reconstruction runs;
    // if either is missing this frame, refuse honestly instead of feeding
    // garbage to the network.
    if (!fc.motion.IsValid() || (!fc.depth_linear.IsValid() && !fc.depth.IsValid())) {
        last_error_ = "missing depth or motion vectors for XeSS execute";
        return { {}, {}, false };
    }
    // XeSS expects normalized [0,1] depth — prefer the DepthProvider's
    // linearized output (same convention the Disocclusion pass consumes);
    // fall back to raw depth when linearization was skipped this frame.
    const core::GpuTexture& depth_tex =
        fc.depth_linear.IsValid() ? fc.depth_linear : fc.depth;
    auto* depth_res  = static_cast<ID3D11Resource*>(depth_tex.Get()->GetNativeResource());
    auto* motion_res = static_cast<ID3D11Resource*>(fc.motion.Get()->GetNativeResource());
    auto* out_res    = static_cast<ID3D11Resource*>(output_texture_.Get()->GetNativeResource());
    if (!depth_res || !motion_res || !out_res) {
        last_error_ = "GPU resources unavailable for XeSS execute";
        return { {}, {}, false };
    }

    // Motion-vector contract (Intel SR guide, "Motion Vectors" + "Velocity
    // Scale"): XeSS expects screen-space motion in PIXELS from the current
    // frame to the previous frame, and its default velocity scale is 1.0 =
    // pixels — exactly the runtime pipeline's MV convention, so no
    // xessSetVelocityScale call is needed. Jitter is passed in the
    // [-0.5, 0.5] pixel contract. Depth: XeSS's default is "smaller = closer",
    // which is what DepthProvider's linearized [0,1] output produces.
    XessD3D11ExecuteParams params{};
    params.pColorTexture               = color_res;
    params.pVelocityTexture            = motion_res;
    params.pDepthTexture               = depth_res;
    params.pExposureScaleTexture       = nullptr;
    params.pResponsivePixelMaskTexture = nullptr;
    params.pOutputTexture              = out_res;
    params.jitter_offset_x             = fc.jitter.jitter_x;
    params.jitter_offset_y             = fc.jitter.jitter_y;
    params.exposure_scale              = 1.0f;
    params.reset_history               = fc.validity.history_valid ? 0u : 1u;
    params.input_width                 = in_w;
    params.input_height                = in_h;
    params.input_color_base            = { 0, 0 };
    params.input_motion_vector_base    = { 0, 0 };
    params.input_depth_base            = { 0, 0 };
    params.input_responsive_mask_base  = { 0, 0 };
    params.reserved0                   = { 0, 0 };
    params.output_color_base           = { 0, 0 };

    (void)cmd_ctx;  // XeSS D3D11 executes on the immediate context internally.

    const int rc = pfn_execute_(context_, &params);
    if (rc != kXessResultSuccess) {
        last_error_ = "xessD3D11Execute failed: ";
        last_error_ += XessResultString(rc);
        return { {}, {}, false };
    }

    // Publish the genuinely reconstructed output.
    fc.color = output_texture_;
    fc.output = output_texture_;
    fc.output_resolution = { out_w, out_h };
    fc.validity.color_valid = true;
    return { output_texture_, { out_w, out_h }, true };
}

}  // namespace omnirender::backends::xess

#endif  // _WIN32
