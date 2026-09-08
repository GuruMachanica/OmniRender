// filepath: modules/daemon/tonemap_neural.h
#pragma once

#include <d3d11.h>

namespace omnirender::daemon {

// Neural inverse tone mapping. Deferred to v0.4.0-alpha. The compute
// fallback lives in processing.cpp. These functions are exported so
// future TensorRT / DirectML integration can be added here without
// touching processing.cpp.

bool InitializeNeuralTonemap();
void DispatchNeuralTonemap(ID3D11DeviceContext* ctx,
                           ID3D11Texture2D* in,
                           ID3D11Texture2D* out);

}  // namespace omnirender::daemon
