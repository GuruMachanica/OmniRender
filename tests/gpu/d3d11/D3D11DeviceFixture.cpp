// filepath: tests/gpu/d3d11/D3D11DeviceFixture.cpp
#include "D3D11DeviceFixture.h"
#include <cstdio>
#include <vector>

namespace omnirender::test::gpu {

static std::string FeatureLevelToString(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_11_1: return "11.1";
        case D3D_FEATURE_LEVEL_11_0: return "11.0";
        case D3D_FEATURE_LEVEL_10_1: return "10.1";
        case D3D_FEATURE_LEVEL_10_0: return "10.0";
        default:                     return "Unknown";
    }
}

static std::string WideToNarrow(const WCHAR* wstr) {
    if (!wstr) return {};
    char buf[256] = {};
    for (size_t i = 0; i < 255 && wstr[i] != L'\0'; ++i) {
        buf[i] = static_cast<char>(wstr[i]);
    }
    return std::string(buf);
}

D3D11DeviceFixture::D3D11DeviceFixture() = default;

D3D11DeviceFixture::~D3D11DeviceFixture() {
    Shutdown();
}

bool D3D11DeviceFixture::Initialize(bool force_warp) {
    if (initialized_) {
        return true;
    }

    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL obtained_level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = E_FAIL;
    bool is_warp = force_warp;

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> selected_adapter;

    if (!force_warp && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf())))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        UINT i = 0;
        size_t max_vram = 0;
        Microsoft::WRL::ComPtr<IDXGIAdapter> best_vram_adapter;
        Microsoft::WRL::ComPtr<IDXGIAdapter> nvidia_adapter;

        while (factory->EnumAdapters(i++, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                if (desc.VendorId == 0x10DE) { // Prioritize NVIDIA for official DLSS hardware execution
                    nvidia_adapter = adapter;
                }
                if (desc.DedicatedVideoMemory > max_vram) {
                    max_vram = desc.DedicatedVideoMemory;
                    best_vram_adapter = adapter;
                }
            }
        }

        if (nvidia_adapter) {
            selected_adapter = nvidia_adapter;
        } else if (best_vram_adapter) {
            selected_adapter = best_vram_adapter;
        }
    }

    if (selected_adapter) {
        hr = D3D11CreateDevice(selected_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               flags, feature_levels, 2, D3D11_SDK_VERSION,
                               device_.GetAddressOf(), &obtained_level,
                               context_.GetAddressOf());
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(selected_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                   flags, feature_levels, 2, D3D11_SDK_VERSION,
                                   device_.GetAddressOf(), &obtained_level,
                                   context_.GetAddressOf());
        }
    }

    if (FAILED(hr) && !force_warp) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               flags, feature_levels, 2, D3D11_SDK_VERSION,
                               device_.GetAddressOf(), &obtained_level,
                               context_.GetAddressOf());
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
            // Retry without debug layer if debug layer is not installed on OS
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, feature_levels, 2, D3D11_SDK_VERSION,
                                   device_.GetAddressOf(), &obtained_level,
                                   context_.GetAddressOf());
        }
    }

    if (FAILED(hr)) {
        is_warp = true;
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               flags, feature_levels, 2, D3D11_SDK_VERSION,
                               device_.GetAddressOf(), &obtained_level,
                               context_.GetAddressOf());
    }

    if (FAILED(hr) || !device_ || !context_) {
        return false;
    }

    QueryAdapterInfo(device_.Get(), obtained_level, is_warp);

    graphics_device_ = std::make_shared<graphics::d3d11::D3D11GraphicsDevice>(device_, context_);
    initialized_ = (graphics_device_ != nullptr);
    return initialized_;
}

bool D3D11DeviceFixture::QueryAdapterInfo(ID3D11Device* device, D3D_FEATURE_LEVEL level, bool is_warp) {
    if (!device) return false;

    adapter_info_.is_warp = is_warp;
    adapter_info_.feature_level_str = FeatureLevelToString(level);

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi_device.GetAddressOf())))) {
        adapter_info_.description = is_warp ? "Microsoft Basic Render Driver (WARP)" : "Generic D3D11 Adapter";
        return true;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgi_adapter;
    if (FAILED(dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf()))) {
        adapter_info_.description = is_warp ? "Microsoft Basic Render Driver (WARP)" : "Generic D3D11 Adapter";
        return true;
    }

    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(dxgi_adapter->GetDesc(&desc))) {
        adapter_info_.description          = WideToNarrow(desc.Description);
        adapter_info_.vendor_id            = desc.VendorId;
        adapter_info_.device_id            = desc.DeviceId;
        adapter_info_.subsys_id            = desc.SubSysId;
        adapter_info_.revision             = desc.Revision;
        adapter_info_.dedicated_vram_bytes = desc.DedicatedVideoMemory;
        adapter_info_.dedicated_sys_bytes  = desc.DedicatedSystemMemory;
        adapter_info_.shared_sys_bytes     = desc.SharedSystemMemory;
    }

    return true;
}

void D3D11DeviceFixture::Shutdown() {
    graphics_device_.reset();
    context_.Reset();
    device_.Reset();
    initialized_ = false;
}

}  // namespace omnirender::test::gpu
