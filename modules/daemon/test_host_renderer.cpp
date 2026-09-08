// filepath: modules/daemon/test_host_renderer.cpp
// D3D11 rendering plumbing for OmniRenderTestHost.

#include "test_host_renderer.h"
#include "test_host_math.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

namespace omnirender::test {

namespace {

constexpr wchar_t kWindowClass[] = L"OmniRenderTestHostWnd";
constexpr wchar_t kWindowTitle[] = L"OmniRenderTestHost";

static constexpr const char* kVertexShaderHLSL = R"(
struct VIn { float3 pos : POSITION; float3 col : COLOR; };
struct VOut { float4 pos : SV_POSITION; float3 col : COLOR; };
cbuffer VP : register(b0) { float4x4 view_proj; };
VOut VSMain(VIn v) {
    VOut o;
    o.pos = mul(float4(v.pos, 1.0f), view_proj);
    o.col = v.col;
    return o;
}
)";

static constexpr const char* kPixelShaderHLSL = R"(
struct PIn { float4 pos : SV_POSITION; float3 col : COLOR; };
float4 PSMain(PIn p) : SV_TARGET { return float4(p.col, 1.0f); }
)";

LRESULT CALLBACK HostWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return ::DefWindowProcW(h, m, w, l);
}

bool CreateHiddenHostWindow(TestHostRenderer& r) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = HostWndProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    RECT rc{ 0, 0, static_cast<LONG>(r.width), static_cast<LONG>(r.height) };
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    r.hwnd = ::CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    return r.hwnd != nullptr;
}

bool CreateHostSwapChain(TestHostRenderer& r) {
    IDXGIDevice*  dxgi_dev  = nullptr;
    IDXGIAdapter* dxgi_adap = nullptr;
    IDXGIFactory2* factory  = nullptr;
    if (FAILED(r.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_dev)))) return false;
    if (FAILED(dxgi_dev->GetAdapter(&dxgi_adap))) { dxgi_dev->Release(); return false; }
    if (FAILED(dxgi_adap->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
        dxgi_adap->Release(); dxgi_dev->Release(); return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width              = r.width;
    desc.Height             = r.height;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount        = 2;
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;

    HRESULT hr = factory->CreateSwapChainForHwnd(
        r.device, r.hwnd, &desc, nullptr, nullptr,
        reinterpret_cast<IDXGISwapChain1**>(&r.swapchain));
    factory->Release();
    dxgi_adap->Release();
    dxgi_dev->Release();
    if (FAILED(hr)) return false;

    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(r.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer)))) return false;
    r.device->CreateRenderTargetView(backbuffer, nullptr, &r.backbuffer_rtv);
    backbuffer->Release();
    return true;
}

bool CreateHostSharedSurfaces(TestHostRenderer& r) {
    D3D11_TEXTURE2D_DESC cdesc{};
    cdesc.Width              = r.width;
    cdesc.Height             = r.height;
    cdesc.MipLevels          = 1;
    cdesc.ArraySize          = 1;
    cdesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    cdesc.SampleDesc.Count   = 1;
    cdesc.Usage              = D3D11_USAGE_DEFAULT;
    cdesc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    cdesc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(r.device->CreateTexture2D(&cdesc, nullptr, &r.shared_color))) return false;

    IDXGIResource* res = nullptr;
    if (FAILED(r.shared_color->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&res)))) return false;
    res->GetSharedHandle(&r.shared_color_h);
    res->Release();
    if (!r.shared_color_h) return false;
    if (FAILED(r.device->CreateRenderTargetView(r.shared_color, nullptr, &r.shared_color_rtv))) return false;

    D3D11_TEXTURE2D_DESC ddesc{};
    ddesc.Width              = r.width;
    ddesc.Height             = r.height;
    ddesc.MipLevels          = 1;
    ddesc.ArraySize          = 1;
    ddesc.Format             = DXGI_FORMAT_R32_TYPELESS;
    ddesc.SampleDesc.Count   = 1;
    ddesc.Usage              = D3D11_USAGE_DEFAULT;
    ddesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ddesc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(r.device->CreateTexture2D(&ddesc, nullptr, &r.shared_depth))) return false;

    res = nullptr;
    if (FAILED(r.shared_depth->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&res)))) return false;
    res->GetSharedHandle(&r.shared_depth_h);
    res->Release();
    if (!r.shared_depth_h) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    return SUCCEEDED(r.device->CreateDepthStencilView(r.shared_depth, &dsvd, &r.shared_depth_dsv));
}

bool CompileHostShaders(TestHostRenderer& r) {
    ID3DBlob* vs_code = nullptr;
    HRESULT hr = D3DCompile(kVertexShaderHLSL, std::strlen(kVertexShaderHLSL),
                            "TestHostVS", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vs_code, nullptr);
    if (FAILED(hr) || !vs_code) return false;

    D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = r.device->CreateInputLayout(layout_desc, 2, vs_code->GetBufferPointer(),
                                     vs_code->GetBufferSize(), &r.layout);
    if (FAILED(hr)) { vs_code->Release(); return false; }

    hr = r.device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &r.vs);
    vs_code->Release();
    if (FAILED(hr)) return false;

    ID3DBlob* ps_code = nullptr;
    hr = D3DCompile(kPixelShaderHLSL, std::strlen(kPixelShaderHLSL),
                    "TestHostPS", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &ps_code, nullptr);
    if (FAILED(hr) || !ps_code) return false;

    hr = r.device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &r.ps);
    ps_code->Release();
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC vbd{ sizeof(kCubeVertices), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vsd{ kCubeVertices, 0, 0 };
    if (FAILED(r.device->CreateBuffer(&vbd, &vsd, &r.vb))) return false;

    D3D11_BUFFER_DESC ibd{ sizeof(kCubeIndices), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA isd{ kCubeIndices, 0, 0 };
    if (FAILED(r.device->CreateBuffer(&ibd, &isd, &r.ib))) return false;

    D3D11_BUFFER_DESC cbd{ sizeof(float) * 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    return SUCCEEDED(r.device->CreateBuffer(&cbd, nullptr, &r.cb));
}

}  // namespace

bool InitializeTestRenderer(TestHostRenderer& r, UINT width, UINT height) {
    r.width = width;
    r.height = height;

    if (!CreateHiddenHostWindow(r)) {
        std::fprintf(stderr, "test_host: CreateHiddenHostWindow failed\n");
        return false;
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter1* best_adapter = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        size_t max_vram = 0;
        for (UINT i = 0;; ++i) {
            IDXGIAdapter1* candidate = nullptr;
            if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!best_adapter || desc.DedicatedVideoMemory > max_vram)) {
                if (best_adapter) best_adapter->Release();
                best_adapter = candidate;
                max_vram = desc.DedicatedVideoMemory;
                continue;
            }
            candidate->Release();
        }
        factory->Release();
    }

    HRESULT hr = D3D11CreateDevice(best_adapter, best_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &r.device, nullptr, &r.context);
    if (best_adapter) best_adapter->Release();
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &r.device, nullptr, &r.context);
    if (FAILED(hr)) return false;

    IDXGIDevice* dxgi_dev = nullptr;
    if (SUCCEEDED(r.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_dev)))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                r.adapter_luid = (static_cast<uint64_t>(desc.AdapterLuid.HighPart) << 32) | desc.AdapterLuid.LowPart;
            }
            adapter->Release();
        }
        dxgi_dev->Release();
    }

    if (!CreateHostSwapChain(r)) {
        std::fprintf(stderr, "test_host: CreateHostSwapChain failed\n");
        return false;
    }
    if (!CreateHostSharedSurfaces(r)) {
        std::fprintf(stderr, "test_host: CreateHostSharedSurfaces failed\n");
        return false;
    }
    if (!CompileHostShaders(r)) {
        std::fprintf(stderr, "test_host: CompileHostShaders failed\n");
        return false;
    }
    return true;
}

void CleanupTestRenderer(TestHostRenderer& r) {
    IUnknown* objs[] = {
        r.cb, r.ib, r.vb, r.layout, r.ps, r.vs,
        r.shared_depth_dsv, r.shared_depth, r.shared_color_rtv,
        r.shared_color, r.backbuffer_rtv, r.swapchain, r.context, r.device
    };
    for (auto* o : objs) if (o) o->Release();
    r.cb = nullptr; r.ib = nullptr; r.vb = nullptr; r.layout = nullptr;
    r.ps = nullptr; r.vs = nullptr; r.shared_depth_dsv = nullptr;
    r.shared_depth = nullptr; r.shared_color_rtv = nullptr;
    r.shared_color = nullptr; r.backbuffer_rtv = nullptr;
    r.swapchain = nullptr; r.context = nullptr; r.device = nullptr;
    if (r.hwnd) { ::DestroyWindow(r.hwnd); r.hwnd = nullptr; }
}

void RenderTestCube(TestHostRenderer& r, double seconds, uint64_t frame_index) {
    float yaw   = static_cast<float>(seconds * 0.6);
    float pitch = static_cast<float>(std::sin(seconds * 0.4) * 0.4);
    float aspect = static_cast<float>(r.width) / static_cast<float>(r.height);
    std::memcpy(r.previous_vp, r.current_vp, sizeof(r.current_vp));
    BuildViewProj(yaw, pitch, aspect, r.current_vp);
    if (frame_index == 0) {
        std::memcpy(r.previous_vp, r.current_vp, sizeof(r.current_vp));
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(r.context->Map(r.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, r.current_vp, sizeof(r.current_vp));
        r.context->Unmap(r.cb, 0);
    }

    ID3D11RenderTargetView* rtv = r.shared_color_rtv;
    r.context->OMSetRenderTargets(1, &rtv, r.shared_depth_dsv);

    D3D11_VIEWPORT vp_dx{ 0.0f, 0.0f, static_cast<float>(r.width), static_cast<float>(r.height), 0.0f, 1.0f };
    r.context->RSSetViewports(1, &vp_dx);

    FLOAT clear[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
    r.context->ClearRenderTargetView(rtv, clear);
    r.context->ClearDepthStencilView(r.shared_depth_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    UINT stride = sizeof(Vertex), offset = 0;
    r.context->IASetInputLayout(r.layout);
    r.context->IASetVertexBuffers(0, 1, &r.vb, &stride, &offset);
    r.context->IASetIndexBuffer(r.ib, DXGI_FORMAT_R16_UINT, 0);
    r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.context->VSSetShader(r.vs, nullptr, 0);
    r.context->VSSetConstantBuffers(0, 1, &r.cb);
    r.context->PSSetShader(r.ps, nullptr, 0);
    r.context->DrawIndexed(_countof(kCubeIndices), 0, 0);
}

}  // namespace omnirender::test
