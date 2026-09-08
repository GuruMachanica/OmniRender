// filepath: modules/daemon/shader_loader.h
#pragma once

#include <d3d11.h>

namespace omnirender::daemon {

// Load precompiled shader byte code (CSO) into a D3D11 compute shader.
bool LoadComputeShader(ID3D11Device* device,
                       const wchar_t* path,
                       ID3D11ComputeShader** out);

// Load or compile fallback fullscreen passthrough vertex and pixel shaders.
void EnsurePassthroughShaders(ID3D11Device* device,
                              ID3D11VertexShader** vs_out,
                              ID3D11PixelShader** ps_out);

}  // namespace omnirender::daemon
