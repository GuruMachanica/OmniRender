// filepath: backends/reconstruction/dlss/DlssLoader.cpp
#include "DlssLoader.h"

namespace omnirender::backends::dlss {

std::wstring DlssLoader::QueryRegistryPath() {
    HKEY h_key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", 0, KEY_READ, &h_key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG res = RegQueryValueExW(h_key, L"FullPath", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(h_key);
    if (res == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return std::wstring(buf);
    }
    return {};
}

bool DlssLoader::LoadLibraries(const std::wstring& explicit_path) {
    if (IsLoaded()) {
        return true;
    }

    // 1. Explicit configured NGX path (if specified)
    if (!explicit_path.empty()) {
        module_ = ::LoadLibraryW(explicit_path.c_str());
    }

    // 2. Application / game directory and standard DLL search paths
    if (!module_) {
        module_ = ::LoadLibraryW(L"_nvngx.dll");
        if (!module_) {
            module_ = ::LoadLibraryW(L"nvngx.dll");
        }
    }

    // 3. Official NVIDIA NGXCore registry discovered path
    if (!module_) {
        std::wstring reg_path = QueryRegistryPath();
        if (!reg_path.empty()) {
            std::wstring cand1 = reg_path + L"\\_nvngx.dll";
            module_ = ::LoadLibraryW(cand1.c_str());
            if (!module_) {
                std::wstring cand2 = reg_path + L"\\nvngx.dll";
                module_ = ::LoadLibraryW(cand2.c_str());
            }
        }
    }

    if (!module_) {
        return false;
    }

    dispatch_.pfn_init            = reinterpret_cast<PFN_NVSDK_NGX_D3D11_Init>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_Init"));
    dispatch_.pfn_shutdown        = reinterpret_cast<PFN_NVSDK_NGX_D3D11_Shutdown>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_Shutdown"));
    dispatch_.pfn_alloc_params    = reinterpret_cast<PFN_NVSDK_NGX_D3D11_AllocateParameters>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_AllocateParameters"));
    // Strict symbol binding: only bind the actual capability API
    dispatch_.pfn_get_cap_params  = reinterpret_cast<PFN_NVSDK_NGX_D3D11_GetCapabilityParameters>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_GetCapabilityParameters"));
    dispatch_.pfn_destroy_params  = reinterpret_cast<PFN_NVSDK_NGX_D3D11_DestroyParameters>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_DestroyParameters"));
    dispatch_.pfn_create_feature  = reinterpret_cast<PFN_NVSDK_NGX_D3D11_CreateFeature>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_CreateFeature"));
    dispatch_.pfn_eval_feature    = reinterpret_cast<PFN_NVSDK_NGX_D3D11_EvaluateFeature>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_EvaluateFeature"));
    dispatch_.pfn_release_feature = reinterpret_cast<PFN_NVSDK_NGX_D3D11_ReleaseFeature>(::GetProcAddress(module_, "NVSDK_NGX_D3D11_ReleaseFeature"));

    if (!dispatch_.IsValid()) {
        Unload();
        return false;
    }

    return true;
}

void DlssLoader::Unload() {
    dispatch_ = {};
    if (module_) {
        ::FreeLibrary(module_);
        module_ = nullptr;
    }
}

DlssLoader::~DlssLoader() {
    Unload();
}

}  // namespace omnirender::backends::dlss
