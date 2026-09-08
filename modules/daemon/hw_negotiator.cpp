// filepath: modules/daemon/hw_negotiator.cpp
// Hardware capability negotiator.
//
// v0.2.0-alpha: vendor/VRAM probe only. The capability matrix
// (audit point #14) is scheduled for v0.5.0. For now, this just
// reports the primary DXGI adapter and picks a coarse profile.

#include <dxgi1_4.h>
#include <iostream>

#include "../common/logging.h"

namespace omnirender::daemon {

enum class ExecutionProfile {
    PROFILE_RTX_TEMPORAL,
    PROFILE_SPATIAL_FALLBACK,
};

static ExecutionProfile NegotiateHardwareCapability(IDXGIAdapter* pAdapter) {
    DXGI_ADAPTER_DESC desc{};
    pAdapter->GetDesc(&desc);
    size_t dedicatedVRAM_MB = desc.DedicatedVideoMemory / (1024 * 1024);
    bool   isNvidia         = (desc.VendorId == 0x10DE);

    std::wcout << L"[OmniRender Negotiator] Adapter: " << desc.Description << std::endl;
    std::cout  << "[OmniRender Negotiator] VRAM: "    << dedicatedVRAM_MB << " MB" << std::endl;
    if (isNvidia && dedicatedVRAM_MB >= 3500) {
        std::cout << "[OmniRender Negotiator] Profile: RTX_TEMPORAL" << std::endl;
        return ExecutionProfile::PROFILE_RTX_TEMPORAL;
    }
    std::cout << "[OmniRender Negotiator] Profile: SPATIAL_FALLBACK" << std::endl;
    return ExecutionProfile::PROFILE_SPATIAL_FALLBACK;
}

int RunHardwareNegotiator(void** ppAdapter) {
    if (!ppAdapter) return -1;
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return -1;

    IDXGIAdapter1* best_adapter = nullptr;
    size_t max_vram = 0;

    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* candidate = nullptr;
        if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            candidate->Release();
            continue;
        }
        if (!best_adapter || desc.DedicatedVideoMemory > max_vram) {
            if (best_adapter) best_adapter->Release();
            best_adapter = candidate;
            max_vram = desc.DedicatedVideoMemory;
            continue;
        }
        candidate->Release();
    }
    factory->Release();

    if (!best_adapter) return -2;
    const auto profile = NegotiateHardwareCapability(best_adapter);
    *ppAdapter = best_adapter;
    return static_cast<int>(profile);
}

}  // namespace omnirender::daemon
