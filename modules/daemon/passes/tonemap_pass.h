// filepath: modules/daemon/passes/tonemap_pass.h
#pragma once

#include <d3d11.h>

namespace omnirender::daemon {

void ExecuteTonemap(ID3D11DeviceContext* ctx,
                    ID3D11ComputeShader* shader,
                    ID3D11Buffer* cb,
                    ID3D11ShaderResourceView* work_srv,
                    ID3D11UnorderedAccessView* work_uav_b,
                    ID3D11Texture2D* work_tex,
                    ID3D11Texture2D* work_tex_b,
                    UINT width, UINT height);

void ExecuteLinearizeDepth(ID3D11DeviceContext* ctx,
                           ID3D11ComputeShader* shader,
                           ID3D11Buffer* cb,
                           ID3D11ShaderResourceView* depth_srv,
                           ID3D11UnorderedAccessView* work_uav_b,
                           ID3D11Texture2D* work_tex,
                           ID3D11Texture2D* work_tex_b,
                           UINT width, UINT height);

}  // namespace omnirender::daemon
