// filepath: modules/daemon/upscalers/xess_adapter.h
#pragma once

#include <d3d11.h>
#include <windows.h>
#include <cstdint>
#include "../../common/backend_interfaces.h"
#include "../../common/backend_capabilities.h"

namespace omnirender::daemon::upscaler {

// Production adapter implementing IReconstructionBackend for Intel XeSS.
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

private:
    bool DetectGpuFeatures(ID3D11Device* device) noexcept;
    bool LoadXessLibraries();
    void UnloadLibraries();

    ID3D11Device*       device_ = nullptr;
    HMODULE             xess_module_ = nullptr;
    void*               xess_context_ = nullptr;
    RuntimeCapabilities runtime_caps_{};
    uint32_t            max_width_ = 0;
    uint32_t            max_height_ = 0;
    bool                initialized_ = false;
    bool                is_intel_gpu_ = false;
};

XessAdapter& GetGlobalXessAdapter();

}  // namespace omnirender::daemon::upscaler
