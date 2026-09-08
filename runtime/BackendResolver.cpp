// filepath: runtime/BackendResolver.cpp
#include "BackendResolver.h"

namespace omnirender::runtime {

PreferredUpscaler BackendResolver::ResolveUpscaler(const core::RuntimeCapabilities& caps,
                                                   PreferredUpscaler user_preference) noexcept {
    if (user_preference != PreferredUpscaler::Auto) {
        if (user_preference == PreferredUpscaler::DLSS && caps.supports_dlss) return PreferredUpscaler::DLSS;
        if (user_preference == PreferredUpscaler::XeSS && caps.supports_xess) return PreferredUpscaler::XeSS;
        if (user_preference == PreferredUpscaler::FSR  && caps.supports_fsr)  return PreferredUpscaler::FSR;
        if (user_preference == PreferredUpscaler::NativeOmni)                 return PreferredUpscaler::NativeOmni;
    }

    if (caps.supports_dlss) return PreferredUpscaler::DLSS;
    if (caps.supports_xess) return PreferredUpscaler::XeSS;
    if (caps.supports_fsr)  return PreferredUpscaler::FSR;

    return PreferredUpscaler::NativeOmni;
}

const char* BackendResolver::GetUpscalerName(PreferredUpscaler upscaler) noexcept {
    switch (upscaler) {
        case PreferredUpscaler::DLSS:       return "NVIDIA DLSS";
        case PreferredUpscaler::XeSS:       return "Intel XeSS";
        case PreferredUpscaler::FSR:        return "AMD FSR";
        case PreferredUpscaler::NativeOmni: return "OmniRender Native";
        default:                            return "Auto";
    }
}

}  // namespace omnirender::runtime
