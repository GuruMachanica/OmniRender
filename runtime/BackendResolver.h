// filepath: runtime/BackendResolver.h
#pragma once

#include <memory>
#include "../core/capability/RuntimeCapabilities.h"
#include "../backends/reconstruction/IReconstructionBackend.h"

namespace omnirender::runtime {

enum class PreferredUpscaler : uint8_t {
    Auto = 0,
    DLSS,
    XeSS,
    FSR,
    NativeOmni
};

class BackendResolver {
public:
    static PreferredUpscaler ResolveUpscaler(const core::RuntimeCapabilities& caps, PreferredUpscaler user_preference) noexcept;
    static const char* GetUpscalerName(PreferredUpscaler upscaler) noexcept;
};

}  // namespace omnirender::runtime
