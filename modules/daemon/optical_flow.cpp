// filepath: modules/daemon/optical_flow.cpp
// Screen-space motion vector synthesis — DEFERRED to v0.4.0-alpha.
//
// v0.2.0-alpha has no processing. The audit recommends engine-provided
// motion vectors over screen-space optical flow (audit point #20),
// so when we add this we will:
//
//   1. Add a depth + camera reprojection motion-vector pass first
//      (no engine dependency, works for every D3D9/11 game).
//   2. Only then consider optical flow as a refinement.
//
// The HLSL shader (modules/shaders/optical_flow_dis.hlsl) is kept as
// a reference; it is not compiled into the daemon in v0.2.0-alpha.

#include <d3d11.h>
#include <windows.h>

#include "../common/logging.h"

namespace omnirender::daemon {

bool InitializeOpticalFlow(ID3D11Device*) {
    OMNI_LOG_INFO("Optical flow: deferred to v0.4.0-alpha (depth+reprojection will land first)");
    return true;
}

void DispatchOpticalFlow(ID3D11DeviceContext*,
                         ID3D11Texture2D*,
                         ID3D11Texture2D*,
                         ID3D11Texture2D*,
                         ID3D11Texture2D*,
                         ID3D11Texture2D*,
                         UINT, UINT) {
    // No-op for v0.2.0-alpha.
}

}  // namespace omnirender::daemon
