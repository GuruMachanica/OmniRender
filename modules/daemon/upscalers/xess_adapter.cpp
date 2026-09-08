// filepath: modules/daemon/upscalers/xess_adapter.cpp
// Intel XeSS neural reconstruction backend adapter implementing IReconstructionBackend.

#include "xess_adapter.h"

#include <dxgi.h>
#include <atomic>
#include "../../common/logging.h"

namespace omnirender::daemon::upscaler {

namespace {

typedef int (*PFN_xessD3D11CreateContext)(ID3D11Device* device, void** pOutContext);
typedef int (*PFN_xessDestroyContext)(void* context);
typedef int (*PFN_xessD3D11Execute)(void* context, ID3D11DeviceContext* pCmdList, const void* pParams);

PFN_xessD3D11CreateContext pfn_xessCreate  = nullptr;
PFN_xessDestroyContext     pfn_xessDestroy = nullptr;
PFN_xessD3D11Execute       pfn_xessExecute = nullptr;

XessAdapter g_global_xess_adapter;

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

    // Intel PCI Vendor ID is 0x8086
    is_intel_gpu_ = (desc.VendorId == 0x8086);
    // XeSS supports Intel Arc/Iris via XMX, and AMD/NVIDIA GPUs via DP4a instructions
    return true;
}

bool XessAdapter::LoadXessLibraries() {
    xess_module_ = ::LoadLibraryW(L"libxess.dll");
    if (!xess_module_) {
        xess_module_ = ::LoadLibraryW(L"xess.dll");
    }

    if (xess_module_) {
        pfn_xessCreate  = reinterpret_cast<PFN_xessD3D11CreateContext>(
            ::GetProcAddress(xess_module_, "xessD3D11CreateContext"));
        pfn_xessDestroy = reinterpret_cast<PFN_xessDestroyContext>(
            ::GetProcAddress(xess_module_, "xessDestroyContext"));
        pfn_xessExecute = reinterpret_cast<PFN_xessD3D11Execute>(
            ::GetProcAddress(xess_module_, "xessD3D11Execute"));
        return (pfn_xessCreate != nullptr);
    }
    return false;
}

void XessAdapter::UnloadLibraries() {
    if (pfn_xessDestroy && xess_context_) {
        pfn_xessDestroy(xess_context_);
        xess_context_ = nullptr;
    }
    pfn_xessCreate = nullptr;
    pfn_xessDestroy = nullptr;
    pfn_xessExecute = nullptr;

    if (xess_module_) {
        ::FreeLibrary(xess_module_);
        xess_module_ = nullptr;
    }
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
        if (pfn_xessCreate && device_) {
            pfn_xessCreate(device_, &xess_context_);
        }
        OMNI_LOG_INFO("XeSS Adapter initialized (Intel XMX/DP4a=%d, SDK=Loaded)", is_intel_gpu_);
    } else {
        runtime_caps_.supports_sr = false;
        OMNI_LOG_INFO("XeSS Adapter in standby (GPU Supported=%d, Runtime DLL=%d)",
                      runtime_caps_.is_gpu_supported, runtime_caps_.is_sdk_loaded);
    }

    initialized_ = true;
    return true;
}

bool XessAdapter::Execute(FrameContext& ctx) {
    if (!initialized_) return false;

    if (!ctx.validity.color_valid || !ctx.validity.depth_valid || !ctx.validity.motion_valid) {
        OMNI_LOG_WARN("XeSS Execute rejected: missing required inputs (color=%d depth=%d motion=%d)",
                      ctx.validity.color_valid, ctx.validity.depth_valid, ctx.validity.motion_valid);
        return false;
    }

    if (runtime_caps_.supports_sr && xess_context_ && device_ && pfn_xessExecute) {
        ID3D11Resource* color_res  = ctx.color.AsResource<ID3D11Resource>();
        ID3D11Resource* depth_res  = ctx.depth.AsResource<ID3D11Resource>();
        ID3D11Resource* motion_res = ctx.motion.AsResource<ID3D11Resource>();

        if (color_res && depth_res && motion_res) {
            // TODO(xess): populate xess_d3d11_execute_params with color/depth/motion/output
            // views and call pfn_xessExecute(xess_context_, d3d11_ctx, &params).
            // Until then we do NOT set DataSource::Reconstructed so the pipeline
            // falls back to the spatial path rather than returning garbage.
            OMNI_LOG_WARN("XeSS Execute: real xessD3D11Execute call not yet implemented; "
                          "falling back to spatial path");
            return false;
        }
    }

    // SDK not loaded or resources unavailable — do not claim reconstruction success.
    OMNI_LOG_WARN("XeSS Execute: SDK unavailable or resources missing; falling back");
    return false;
}

void XessAdapter::Shutdown() {
    UnloadLibraries();
    device_ = nullptr;
    initialized_ = false;
    runtime_caps_ = {};
    is_intel_gpu_ = false;
}

}  // namespace omnirender::daemon::upscaler
