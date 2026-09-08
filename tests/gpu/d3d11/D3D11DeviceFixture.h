// filepath: tests/gpu/d3d11/D3D11DeviceFixture.h
#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include "../../../graphics/d3d11/D3D11GraphicsDevice.h"

namespace omnirender::test::gpu {

struct GpuAdapterInfo {
    std::string description;
    uint32_t    vendor_id = 0;
    uint32_t    device_id = 0;
    uint32_t    subsys_id = 0;
    uint32_t    revision  = 0;
    size_t      dedicated_vram_bytes = 0;
    size_t      dedicated_sys_bytes  = 0;
    size_t      shared_sys_bytes     = 0;
    bool        is_warp = false;
    std::string feature_level_str;
};

class D3D11DeviceFixture {
public:
    D3D11DeviceFixture();
    ~D3D11DeviceFixture();

    bool Initialize(bool force_warp = false);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }
    [[nodiscard]] const GpuAdapterInfo& GetAdapterInfo() const noexcept { return adapter_info_; }

    [[nodiscard]] ID3D11Device* GetDevice() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D11DeviceContext* GetContext() const noexcept { return context_.Get(); }
    [[nodiscard]] std::shared_ptr<graphics::d3d11::D3D11GraphicsDevice> GetGraphicsDevice() const noexcept {
        return graphics_device_;
    }

private:
    bool QueryAdapterInfo(ID3D11Device* device, D3D_FEATURE_LEVEL level, bool is_warp);

    bool                                                 initialized_ = false;
    GpuAdapterInfo                                       adapter_info_{};
    Microsoft::WRL::ComPtr<ID3D11Device>                 device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>          context_;
    std::shared_ptr<graphics::d3d11::D3D11GraphicsDevice> graphics_device_;
};

}  // namespace omnirender::test::gpu
