// filepath: modules/daemon/test_host_renderer.h
#pragma once

#include <d3d11.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <cstdint>

namespace omnirender::test {

struct TestHostRenderer {
    HWND                    hwnd            = nullptr;
    ID3D11Device*           device          = nullptr;
    ID3D11DeviceContext*    context         = nullptr;
    IDXGISwapChain1*        swapchain       = nullptr;
    ID3D11RenderTargetView* backbuffer_rtv  = nullptr;

    ID3D11Texture2D*        shared_color    = nullptr;
    HANDLE                  shared_color_h  = nullptr;
    ID3D11RenderTargetView* shared_color_rtv = nullptr;

    ID3D11Texture2D*        shared_depth    = nullptr;
    HANDLE                  shared_depth_h  = nullptr;
    ID3D11DepthStencilView* shared_depth_dsv = nullptr;

    ID3D11VertexShader*     vs              = nullptr;
    ID3D11PixelShader*      ps              = nullptr;
    ID3D11InputLayout*      layout          = nullptr;
    ID3D11Buffer*           vb              = nullptr;
    ID3D11Buffer*           ib              = nullptr;
    ID3D11Buffer*           cb              = nullptr;

    UINT                    width           = 1280;
    UINT                    height          = 720;
    uint64_t                adapter_luid    = 0;
    float                   current_vp[16]  {};
    float                   previous_vp[16] {};
};

bool InitializeTestRenderer(TestHostRenderer& r, UINT width, UINT height);
void CleanupTestRenderer(TestHostRenderer& r);
void RenderTestCube(TestHostRenderer& r, double seconds, uint64_t frame_index);

}  // namespace omnirender::test
