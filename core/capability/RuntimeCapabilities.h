// filepath: core/capability/RuntimeCapabilities.h
#pragma once

#include "Platform.h"
#include "GraphicsApi.h"

namespace omnirender::core {

struct RuntimeCapabilities {
    Platform    platform     = Platform::Unknown;
    GraphicsApi graphics_api = GraphicsApi::Unknown;

    bool supports_compute        = false;
    bool supports_async_compute  = false;
    bool supports_fp16           = false;
    bool supports_ray_tracing    = false;
    bool supports_optical_flow   = false;
    bool supports_dlss           = false;
    bool supports_xess           = false;
    bool supports_fsr            = false;
    bool supports_shared_handles = false;

    uint32_t dedicated_vram_mb   = 0;
};

}  // namespace omnirender::core
