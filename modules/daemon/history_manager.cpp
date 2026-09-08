// filepath: modules/daemon/history_manager.cpp
#include "history_manager.h"
#include "../common/logging.h"

namespace omnirender::daemon {

HRESULT HistoryManager::Initialize(ID3D11Device* device, UINT width, UINT height) {
    if (!device || width == 0 || height == 0) return E_INVALIDARG;
    Shutdown();

    width_ = width;
    height_ = height;

    D3D11_TEXTURE2D_DESC h_desc{
        width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0
    };

    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (FAILED(device->CreateTexture2D(&h_desc, nullptr, &history_[i]))) {
            Shutdown();
            return E_FAIL;
        }
        device->CreateShaderResourceView(history_[i], nullptr, &history_srv_[i]);
    }

    D3D11_TEXTURE2D_DESC d_desc{
        width, height, 1, 1, DXGI_FORMAT_R32_FLOAT,
        {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0
    };
    if (SUCCEEDED(device->CreateTexture2D(&d_desc, nullptr, &previous_depth_))) {
        device->CreateShaderResourceView(previous_depth_, nullptr, &previous_depth_srv_);
    }

    write_index_ = 0;
    valid_frames_ = 0;
    state_ = HistoryState::Empty;
    has_previous_depth_ = false;
    return S_OK;
}

void HistoryManager::Shutdown() {
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (history_srv_[i]) { history_srv_[i]->Release(); history_srv_[i] = nullptr; }
        if (history_[i])     { history_[i]->Release();     history_[i] = nullptr; }
    }
    if (previous_depth_srv_) { previous_depth_srv_->Release(); previous_depth_srv_ = nullptr; }
    if (previous_depth_)     { previous_depth_->Release();     previous_depth_ = nullptr; }

    width_ = 0;
    height_ = 0;
    write_index_ = 0;
    valid_frames_ = 0;
    state_ = HistoryState::Empty;
    has_previous_depth_ = false;
}

void HistoryManager::Invalidate(InvalidationReason reason) {
    state_ = (valid_frames_ > 0) ? HistoryState::Invalidated : HistoryState::Empty;
    valid_frames_ = 0;
    has_previous_depth_ = false;

    const char* reason_str = "Generic";
    switch (reason) {
        case InvalidationReason::ResolutionChanged:   reason_str = "ResolutionChanged"; break;
        case InvalidationReason::CameraDiscontinuity: reason_str = "CameraDiscontinuity"; break;
        case InvalidationReason::SceneCut:            reason_str = "SceneCut"; break;
        case InvalidationReason::DeviceReset:         reason_str = "DeviceReset"; break;
        case InvalidationReason::SwapchainRecreated:  reason_str = "SwapchainRecreated"; break;
        case InvalidationReason::HDRChanged:          reason_str = "HDRChanged"; break;
        case InvalidationReason::FormatChanged:       reason_str = "FormatChanged"; break;
        case InvalidationReason::FrameDropped:        reason_str = "FrameDropped"; break;
        case InvalidationReason::AltTab:              reason_str = "AltTab"; break;
        default: break;
    }
    OMNI_LOG_INFO("HistoryManager: Invalidation triggered (%s) -> State: %s", reason_str, GetStateName());
}

const char* HistoryManager::GetStateName() const noexcept {
    switch (state_) {
        case HistoryState::Empty:       return "Empty";
        case HistoryState::WarmingUp:   return "WarmingUp";
        case HistoryState::Valid:       return "Valid";
        case HistoryState::Invalidated: return "Invalidated";
        case HistoryState::Rebuilding:  return "Rebuilding";
        default:                        return "Unknown";
    }
}

ID3D11ShaderResourceView* HistoryManager::GetCurrentHistorySrv() const noexcept {
    if (valid_frames_ == 0) return nullptr;
    UINT read_idx = (write_index_ + kMaxSlots - 1) % kMaxSlots;
    return history_srv_[read_idx];
}

ID3D11Texture2D* HistoryManager::GetCurrentHistoryTexture() const noexcept {
    if (valid_frames_ == 0) return nullptr;
    UINT read_idx = (write_index_ + kMaxSlots - 1) % kMaxSlots;
    return history_[read_idx];
}

ID3D11ShaderResourceView* HistoryManager::GetPreviousDepthSrv() const noexcept {
    return has_previous_depth_ ? previous_depth_srv_ : nullptr;
}

void HistoryManager::CommitFrame(ID3D11DeviceContext* ctx,
                                 ID3D11Texture2D* resolved_color,
                                 ID3D11Texture2D* current_depth) {
    if (!ctx) return;

    if (resolved_color && history_[write_index_]) {
        D3D11_TEXTURE2D_DESC src_desc{};
        resolved_color->GetDesc(&src_desc);
        D3D11_TEXTURE2D_DESC dst_desc{};
        history_[write_index_]->GetDesc(&dst_desc);
        if (src_desc.Format == dst_desc.Format &&
            src_desc.Width == dst_desc.Width &&
            src_desc.Height == dst_desc.Height) {
            ctx->CopyResource(history_[write_index_], resolved_color);
            write_index_ = (write_index_ + 1) % kMaxSlots;
            if (valid_frames_ < kMaxSlots) {
                valid_frames_++;
            }
            if (valid_frames_ >= 2) {
                state_ = HistoryState::Valid;
            } else if (state_ == HistoryState::Invalidated) {
                state_ = HistoryState::Rebuilding;
            } else {
                state_ = HistoryState::WarmingUp;
            }
        }
    }

    if (current_depth && previous_depth_) {
        D3D11_TEXTURE2D_DESC cur_d{};
        current_depth->GetDesc(&cur_d);
        D3D11_TEXTURE2D_DESC prev_d{};
        previous_depth_->GetDesc(&prev_d);
        if (cur_d.Format == prev_d.Format &&
            cur_d.Width == prev_d.Width &&
            cur_d.Height == prev_d.Height) {
            ctx->CopyResource(previous_depth_, current_depth);
            has_previous_depth_ = true;
        }
    }
}

}  // namespace omnirender::daemon
