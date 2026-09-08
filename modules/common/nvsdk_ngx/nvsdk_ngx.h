// filepath: modules/common/nvsdk_ngx/nvsdk_ngx.h
#pragma once

#include <d3d11.h>
#include <windows.h>
#include "nvsdk_ngx_defs.h"
#include "nvsdk_ngx_params.h"

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_FeatureDiscoveryInfo;

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_Init)(
    unsigned long long InApplicationId,
    const wchar_t* InApplicationDataPath,
    ID3D11Device* InDevice,
    const NVSDK_NGX_FeatureDiscoveryInfo* InFeatureDiscovery,
    NVSDK_NGX_Version InSDKVersion
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_Shutdown)();

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_AllocateParameters)(
    NVSDK_NGX_Parameter** OutParameters
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_GetCapabilityParameters)(
    NVSDK_NGX_Parameter** OutParameters
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_GetParameters)(
    NVSDK_NGX_Parameter** OutParameters
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_DestroyParameters)(
    NVSDK_NGX_Parameter* InParameters
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_CreateFeature)(
    ID3D11DeviceContext* InDevCtx,
    NVSDK_NGX_Feature InFeatureID,
    NVSDK_NGX_Parameter* InParameters,
    NVSDK_NGX_Handle** OutHandle
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_EvaluateFeature)(
    ID3D11DeviceContext* InDevCtx,
    const NVSDK_NGX_Handle* InFeatureHandle,
    const NVSDK_NGX_Parameter* InParameters,
    void* InCallback
);

typedef NVSDK_NGX_Result (NVSDK_NGX_API *PFN_NVSDK_NGX_D3D11_ReleaseFeature)(
    NVSDK_NGX_Handle* InHandle
);
