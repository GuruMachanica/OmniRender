// filepath: modules/daemon/interop_d3d11.cpp
// Daemon-side zero-copy ingestion of hook-published GPU resources.

#include "interop_d3d11.h"

#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <iostream>

#include "../common/logging.h"
#include "ipc_server.h"

namespace omnirender::daemon {

class HostResourceConsumer {
public:
    ID3D11Device*        m_pDevice      = nullptr;
    ID3D11Device1*       m_pDevice1     = nullptr;
    ID3D11DeviceContext* m_pContext     = nullptr;
    uint64_t             m_adapter_luid = 0;

    ~HostResourceConsumer() {
        Release();
    }

    void Release() {
        if (m_pDevice1) { m_pDevice1->Release(); m_pDevice1 = nullptr; }
        if (m_pContext) { m_pContext->Release(); m_pContext = nullptr; }
        if (m_pDevice)  { m_pDevice->Release();  m_pDevice  = nullptr; }
    }

    IDXGIAdapter1* FindAdapter(uint64_t target_luid) {
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
        IDXGIAdapter1* best = nullptr;
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
            uint64_t luid = (static_cast<uint64_t>(desc.AdapterLuid.HighPart) << 32) | desc.AdapterLuid.LowPart;
            if (target_luid != 0 && luid == target_luid) {
                if (best) best->Release();
                best = candidate;
                break;
            }
            if (target_luid == 0 && desc.DedicatedVideoMemory > max_vram) {
                if (best) best->Release();
                best = candidate;
                max_vram = desc.DedicatedVideoMemory;
                continue;
            }
            candidate->Release();
        }
        factory->Release();
        return best;
    }

    bool Initialize(void* pInAdapter = nullptr) {
        uint64_t target_luid = GetRingAdapterLuid();
        IDXGIAdapter1* chosen_adapter = static_cast<IDXGIAdapter1*>(pInAdapter);
        if (chosen_adapter) chosen_adapter->AddRef();

        if (target_luid != 0 && (!chosen_adapter || m_adapter_luid != target_luid)) {
            IDXGIAdapter1* matched = FindAdapter(target_luid);
            if (matched) {
                if (chosen_adapter) chosen_adapter->Release();
                chosen_adapter = matched;
            }
        }
        if (!chosen_adapter) {
            chosen_adapter = FindAdapter(0);
        }

        uint64_t candidate_luid = 0;
        if (chosen_adapter) {
            DXGI_ADAPTER_DESC1 desc{};
            chosen_adapter->GetDesc1(&desc);
            candidate_luid = (static_cast<uint64_t>(desc.AdapterLuid.HighPart) << 32) | desc.AdapterLuid.LowPart;
            if (m_pDevice && m_adapter_luid == candidate_luid) {
                chosen_adapter->Release();
                return true;
            }
        }

        Release();

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        const D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        HRESULT hr = D3D11CreateDevice(
            chosen_adapter,
            chosen_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, flags, feature_levels, 2, D3D11_SDK_VERSION,
            &m_pDevice, nullptr, &m_pContext);

        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(
                chosen_adapter,
                chosen_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                nullptr, flags, feature_levels, 2, D3D11_SDK_VERSION,
                &m_pDevice, nullptr, &m_pContext);
        }

        if (chosen_adapter) {
            chosen_adapter->Release();
        }

        if (FAILED(hr)) {
            OMNI_LOG_ERROR("D3D11CreateDevice failed: 0x%08lx", hr);
            return false;
        }

        m_adapter_luid = candidate_luid;
        m_pDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&m_pDevice1));
        OMNI_LOG_INFO("D3D11 Interop Device initialized (adapter LUID: 0x%llx)", m_adapter_luid);
        return true;
    }

    bool ImportSharedSurface(HANDLE hSharedHandle, ID3D11Texture2D** ppOutTexture) {
        if (!hSharedHandle || !m_pDevice) return false;

        uint64_t ring_luid = GetRingAdapterLuid();
        if (ring_luid != 0 && ring_luid != m_adapter_luid) {
            OMNI_LOG_INFO("Adapter LUID mismatch (device=0x%llx, ring=0x%llx), reinitializing interop",
                          m_adapter_luid, ring_luid);
            if (!Initialize(nullptr)) return false;
        }

        HRESULT hr = E_FAIL;
        if (m_pDevice1) {
            hr = m_pDevice1->OpenSharedResource1(
                hSharedHandle,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(ppOutTexture));
        }
        if (FAILED(hr)) {
            hr = m_pDevice->OpenSharedResource(
                hSharedHandle,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(ppOutTexture));
        }
        if (FAILED(hr)) {
            HRESULT reason = m_pDevice->GetDeviceRemovedReason();
            OMNI_LOG_ERROR("OpenSharedResource failed: 0x%08lx (removed reason: 0x%08lx)", hr, reason);
            return false;
        }
        return true;
    }
};

static HostResourceConsumer g_consumer;

bool InitializeInterop(void* pAdapter) {
    return g_consumer.Initialize(pAdapter);
}

ID3D11Device* Device()        { return g_consumer.m_pDevice; }
ID3D11DeviceContext* Context() { return g_consumer.m_pContext; }
uint64_t CurrentAdapterLuid() { return g_consumer.m_adapter_luid; }

bool ImportColorHandle(HANDLE h, ID3D11Texture2D** out) {
    return g_consumer.ImportSharedSurface(h, out);
}

bool ImportDepthHandle(HANDLE h, ID3D11Texture2D** out) {
    return g_consumer.ImportSharedSurface(h, out);
}

}  // namespace omnirender::daemon

