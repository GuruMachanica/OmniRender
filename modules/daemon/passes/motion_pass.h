// filepath: modules/daemon/passes/motion_pass.h
#pragma once

#include <d3d11.h>
#include <cstdint>
#include "../../common/ipc_protocol.h"

namespace omnirender::daemon {

int ExecuteMotionReproject(ID3D11DeviceContext* ctx,
                           ID3D11ComputeShader* shader,
                           ID3D11Buffer* cb,
                           ID3D11ShaderResourceView* depth_srv,
                           ID3D11ShaderResourceView* prev_depth_srv,
                           ID3D11UnorderedAccessView* motion_uav,
                           const OmniRenderIPCFrameData& payload,
                           bool* out_motion_valid = nullptr);

int ExecuteReactiveMask(ID3D11DeviceContext* ctx,
                        ID3D11ComputeShader* shader,
                        ID3D11Buffer* cb,
                        ID3D11ShaderResourceView* cur_srv,
                        ID3D11ShaderResourceView* prev_srv,
                        ID3D11ShaderResourceView* depth_srv,
                        ID3D11UnorderedAccessView* reactive_uav,
                        const OmniRenderIPCFrameData& payload);

}  // namespace omnirender::daemon
