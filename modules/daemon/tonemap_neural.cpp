// filepath: modules/daemon/tonemap_neural.cpp
// Neural inverse tone mapping — DEFERRED to v0.4.0-alpha.
//
// v0.3.0-alpha ships a compute-shader fallback inside processing.cpp.
// When TensorRT / DirectML is wired in, this module will load the FP16
// engine and dispatch the network on the bounded work texture.

#include <d3d11.h>
#include <windows.h>

#include "../common/logging.h"

namespace omnirender::daemon {

bool InitializeNeuralTonemap() {
    OMNI_LOG_INFO("Neural tonemap: deferred to v0.4.0-alpha (compute fallback is active)");
    return true;
}

void DispatchNeuralTonemap(ID3D11DeviceContext* /*ctx*/,
                           ID3D11Texture2D* /*in*/,
                           ID3D11Texture2D* /*out*/) {
    // No-op. The compute path is in processing.cpp. The neural path
    // will be added here when TensorRT is integrated.
}

}  // namespace omnirender::daemon
