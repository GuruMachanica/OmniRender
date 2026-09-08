// filepath: modules/common/gpu_resource.h
#pragma once

#include <cstdint>

namespace omnirender {

// Explicit ownership semantics for shared and internal GPU memory.
enum class ResourceOwnership : uint8_t {
    GameOwned = 0,
    OmniRenderOwned,
    SharedRead,
    SharedWrite,
    Retired
};

// Explicit lifecycle progression of GPU resources.
enum class ResourceLifetimeState : uint8_t {
    Uninitialized = 0,
    Allocated,
    Acquired,
    InUse,
    Released
};

// High-level pixel format taxonomy.
enum class TextureFormat : uint8_t {
    Unknown = 0,
    R8G8B8A8_UNORM,
    R8G8B8A8_UNORM_SRGB,
    B8G8R8A8_UNORM,
    R16G16B16A16_FLOAT,
    R16G16_FLOAT,
    R32_FLOAT,
    R24G8_TYPELESS,
    D32_FLOAT
};

// Explicit resource metadata including ownership, state, and fence synchronization.
struct GpuResourceDescriptor {
    uint32_t              width = 0;
    uint32_t              height = 0;
    TextureFormat         format = TextureFormat::Unknown;
    ResourceOwnership     ownership = ResourceOwnership::OmniRenderOwned;
    ResourceLifetimeState state = ResourceLifetimeState::Uninitialized;
    uint64_t              fence_value = 0;
    bool                  is_shared = false;
};

// API-independent GPU texture representation with explicit ownership & state transitions.
class GpuTexture {
public:
    GpuTexture() = default;
    GpuTexture(void* native_resource, const GpuResourceDescriptor& desc)
        : native_resource_(native_resource), desc_(desc) {}

    void* GetNativeResource() const noexcept { return native_resource_; }
    const GpuResourceDescriptor& GetDescriptor() const noexcept { return desc_; }

    ResourceOwnership GetOwnership() const noexcept { return desc_.ownership; }
    ResourceLifetimeState GetLifetimeState() const noexcept { return desc_.state; }
    uint64_t GetFenceValue() const noexcept { return desc_.fence_value; }

    bool IsValid() const noexcept { return native_resource_ != nullptr; }

    // Validates whether a state transition is legal in the GPU lifecycle machine.
    bool CanTransitionTo(ResourceLifetimeState next) const noexcept {
        switch (desc_.state) {
            case ResourceLifetimeState::Uninitialized:
                return next == ResourceLifetimeState::Allocated;
            case ResourceLifetimeState::Allocated:
                return next == ResourceLifetimeState::Acquired || next == ResourceLifetimeState::Released;
            case ResourceLifetimeState::Acquired:
                return next == ResourceLifetimeState::InUse || next == ResourceLifetimeState::Released;
            case ResourceLifetimeState::InUse:
                return next == ResourceLifetimeState::InUse || next == ResourceLifetimeState::Released;
            case ResourceLifetimeState::Released:
                return next == ResourceLifetimeState::Acquired || next == ResourceLifetimeState::Allocated;
            default:
                return false;
        }
    }

    bool TransitionTo(ResourceLifetimeState next) noexcept {
        if (!CanTransitionTo(next)) return false;
        desc_.state = next;
        return true;
    }

    bool TransferOwnership(ResourceOwnership next_owner, uint64_t fence = 0) noexcept {
        if (desc_.ownership == ResourceOwnership::Retired) return false;
        desc_.ownership = next_owner;
        if (fence != 0) desc_.fence_value = fence;
        return true;
    }

    bool Acquire() noexcept { return TransitionTo(ResourceLifetimeState::Acquired); }
    bool MarkInUse() noexcept { return TransitionTo(ResourceLifetimeState::InUse); }
    bool Release() noexcept { return TransitionTo(ResourceLifetimeState::Released); }

private:
    void*                 native_resource_ = nullptr;
    GpuResourceDescriptor desc_{};
};

}  // namespace omnirender
