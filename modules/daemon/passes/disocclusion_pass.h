// filepath: modules/daemon/passes/disocclusion_pass.h
#pragma once

#include <d3d11.h>
#include "../../common/ipc_protocol.h"

namespace omnirender::daemon {

int ExecuteDisocclusionMask(ID3D11DeviceContext* ctx,
                            ID3D11ComputeShader* shader,
                            ID3D11Buffer* cb,
                            ID3D11ShaderResourceView* cur_depth_srv,
                            ID3D11ShaderResourceView* prev_depth_srv,
                            ID3D11ShaderResourceView* motion_srv,
                            ID3D11UnorderedAccessView* disocclusion_uav,
                            const OmniRenderIPCFrameData& payload);

}  // namespace omnirender::daemon
