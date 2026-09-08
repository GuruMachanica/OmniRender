// filepath: backends/reconstruction/dlss/DlssLoader.h
#pragma once

#include <windows.h>
#include <string>
#include <string_view>
#include "../../../modules/common/nvsdk_ngx/nvsdk_ngx.h"

namespace omnirender::backends::dlss {

struct DlssNgxDispatchTable {
    PFN_NVSDK_NGX_D3D11_Init                    pfn_init            = nullptr;
    PFN_NVSDK_NGX_D3D11_Shutdown                pfn_shutdown        = nullptr;
    PFN_NVSDK_NGX_D3D11_AllocateParameters      pfn_alloc_params    = nullptr;
    PFN_NVSDK_NGX_D3D11_GetCapabilityParameters pfn_get_cap_params  = nullptr;
    PFN_NVSDK_NGX_D3D11_DestroyParameters       pfn_destroy_params  = nullptr;
    PFN_NVSDK_NGX_D3D11_CreateFeature           pfn_create_feature  = nullptr;
    PFN_NVSDK_NGX_D3D11_EvaluateFeature         pfn_eval_feature    = nullptr;
    PFN_NVSDK_NGX_D3D11_ReleaseFeature          pfn_release_feature = nullptr;

    [[nodiscard]] bool IsValid() const noexcept {
        return pfn_init && pfn_create_feature && pfn_eval_feature && pfn_release_feature;
    }
};

class DlssLoader {
public:
    DlssLoader() = default;
    ~DlssLoader();

    DlssLoader(const DlssLoader&) = delete;
    DlssLoader& operator=(const DlssLoader&) = delete;

    bool LoadLibraries(const std::wstring& explicit_path = L"");
    void Unload();

    [[nodiscard]] bool IsLoaded() const noexcept { return module_ != nullptr && dispatch_.IsValid(); }
    [[nodiscard]] const DlssNgxDispatchTable& GetDispatch() const noexcept { return dispatch_; }
    [[nodiscard]] HMODULE GetModule() const noexcept { return module_; }

private:
    static std::wstring QueryRegistryPath();

    HMODULE              module_ = nullptr;
    DlssNgxDispatchTable dispatch_{};
};

}  // namespace omnirender::backends::dlss
