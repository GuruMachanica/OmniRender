// filepath: modules/common/nvsdk_ngx/nvsdk_ngx_defs.h
#pragma once

#include <cstdint>

#define NVSDK_NGX_APP_ID 0x4F4D4E49ULL // "OMNI" Project ID
#define NVSDK_NGX_API __cdecl

#define NVSDK_NGX_SUCCEED(res) (((res) == 1) || ((res) == 0))
#define NVSDK_NGX_FAILED(res)  (!NVSDK_NGX_SUCCEED(res))

typedef enum NVSDK_NGX_Result {
    NVSDK_NGX_Result_Fail = 0,
    NVSDK_NGX_Result_Success = 1
} NVSDK_NGX_Result;

typedef enum NVSDK_NGX_Version {
    NVSDK_NGX_Version_API = 0x00000013
} NVSDK_NGX_Version;

typedef enum NVSDK_NGX_Feature {
    NVSDK_NGX_Feature_Reserved = 0,
    NVSDK_NGX_Feature_SuperSampling = 1,
    NVSDK_NGX_Feature_SuperResolution = 1,
    NVSDK_NGX_Feature_RayReconstruction = 2,
    NVSDK_NGX_Feature_FrameGeneration = 3
} NVSDK_NGX_Feature;

// Canonical NVIDIA NGX parameter string keys
constexpr const char* NVSDK_NGX_Parameter_Color = "Color";
constexpr const char* NVSDK_NGX_Parameter_Output = "Output";
constexpr const char* NVSDK_NGX_Parameter_Depth = "Depth";
constexpr const char* NVSDK_NGX_Parameter_MotionVectors = "MotionVectors";
constexpr const char* NVSDK_NGX_Parameter_Jitter_Offset_X = "Jitter.Offset.X";
constexpr const char* NVSDK_NGX_Parameter_Jitter_Offset_Y = "Jitter.Offset.Y";
constexpr const char* NVSDK_NGX_Parameter_Sharpness = "Sharpness";
constexpr const char* NVSDK_NGX_Parameter_Reset = "Reset";
constexpr const char* NVSDK_NGX_Parameter_Width = "Width";
constexpr const char* NVSDK_NGX_Parameter_Height = "Height";
constexpr const char* NVSDK_NGX_Parameter_OutWidth = "OutWidth";
constexpr const char* NVSDK_NGX_Parameter_OutHeight = "OutHeight";
constexpr const char* NVSDK_NGX_Parameter_ExposureTexture = "ExposureTexture";
constexpr const char* NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask = "DLSS.Input.Bias.Current.Color.Mask";
constexpr const char* NVSDK_NGX_Parameter_CameraNear = "CameraNear";
constexpr const char* NVSDK_NGX_Parameter_CameraFar = "CameraFar";
constexpr const char* NVSDK_NGX_Parameter_CameraReversedZ = "CameraReversedZ";

// Motion Vector scaling and coordinate spaces
constexpr const char* NVSDK_NGX_Parameter_MV_Scale_X = "MV.Scale.X";
constexpr const char* NVSDK_NGX_Parameter_MV_Scale_Y = "MV.Scale.Y";
constexpr const char* NVSDK_NGX_Parameter_PreExposure = "PreExposure";
constexpr const char* NVSDK_NGX_Parameter_PerfQualityValue = "PerfQualityValue";
constexpr const char* NVSDK_NGX_Parameter_CreationNodeMask = "CreationNodeMask";
constexpr const char* NVSDK_NGX_Parameter_VisibilityNodeMask = "VisibilityNodeMask";
constexpr const char* NVSDK_NGX_Parameter_SuperResolution_Available = "SuperResolution.Available";
constexpr const char* NVSDK_NGX_Parameter_SuperResolution_MinDriverVersionMajor = "SuperResolution.MinDriverVersionMajor";
constexpr const char* NVSDK_NGX_Parameter_SuperResolution_MinDriverVersionMinor = "SuperResolution.MinDriverVersionMinor";

// Subrect dimensions
constexpr const char* NVSDK_NGX_Parameter_DLSS_Subrect_Base_X = "DLSS.Subrect.Base.X";
constexpr const char* NVSDK_NGX_Parameter_DLSS_Subrect_Base_Y = "DLSS.Subrect.Base.Y";
constexpr const char* NVSDK_NGX_Parameter_DLSS_Subrect_Width  = "DLSS.Subrect.Width";
constexpr const char* NVSDK_NGX_Parameter_DLSS_Subrect_Height = "DLSS.Subrect.Height";

typedef enum NVSDK_NGX_PerfQuality_Value {
    NVSDK_NGX_PerfQuality_Value_MaxPerf = 0,
    NVSDK_NGX_PerfQuality_Value_Balanced = 1,
    NVSDK_NGX_PerfQuality_Value_MaxQuality = 2,
    NVSDK_NGX_PerfQuality_Value_UltraPerformance = 3,
    NVSDK_NGX_PerfQuality_Value_UltraQuality = 4
} NVSDK_NGX_PerfQuality_Value;

constexpr const char* NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags = "DLSS.Feature.Create.Flags";

typedef enum NVSDK_NGX_DLSS_Feature_Flags {
    NVSDK_NGX_DLSS_Feature_Flags_None          = 0,
    NVSDK_NGX_DLSS_Feature_Flags_IsHDR         = 1 << 0,
    NVSDK_NGX_DLSS_Feature_Flags_MVLowRes      = 1 << 1,
    NVSDK_NGX_DLSS_Feature_Flags_MVJittered    = 1 << 2,
    NVSDK_NGX_DLSS_Feature_Flags_DepthInverted = 1 << 3,
    NVSDK_NGX_DLSS_Feature_Flags_DoSharpening  = 1 << 5,
    NVSDK_NGX_DLSS_Feature_Flags_AutoExposure   = 1 << 6,
    NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling = 1 << 7
} NVSDK_NGX_DLSS_Feature_Flags;
