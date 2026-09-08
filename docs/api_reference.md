# OmniRender API Reference

## 1. Platform-Independent Core APIs

### `core::FrameContext` ([`core/frame/FrameContext.h`](../core/frame/FrameContext.h))
The primary execution context passed across passes in the `RenderGraph`:

```cpp
struct FrameContext {
    GpuTexture     color;
    GpuTexture     depth;
    GpuTexture     motion;
    GpuTexture     reactive;
    GpuTexture     disocclusion;
    GpuTexture     history;
    GpuTexture     output;

    CameraState    camera;
    JitterState    jitter;
    FrameTiming    timing;

    Resolution     input_resolution;
    Resolution     output_resolution;

    GraphicsApi    graphics_api;
    FrameValidity  validity;
};
```

### `core::RenderGraph` ([`core/graph/RenderGraph.h`](../core/graph/RenderGraph.h))
Directed acyclic pass graph executing modular post-processing passes:

```cpp
class RenderGraph {
public:
    void AddPass(PassType type,
                 std::string name,
                 FrameValidity prerequisites,
                 ResourceAccess reads,
                 ResourceAccess writes,
                 PassExecutionFn execute);

    bool Execute(FrameContext& frame_ctx, graphics::ICommandContext& cmd_ctx);
};
```

### `core::HistoryManager` ([`core/temporal/HistoryManager.h`](../core/temporal/HistoryManager.h))
Platform-independent multi-slot temporal accumulation state machine:

```cpp
class HistoryManager {
public:
    bool Initialize(graphics::IGraphicsDevice& device, uint32_t width, uint32_t height,
                    TextureFormat color_fmt, TextureFormat depth_fmt);
    void Invalidate(InvalidationReason reason);
    GpuTexture GetCurrentHistoryTexture() const noexcept;
    GpuTexture GetPreviousDepthTexture() const noexcept;
    void CommitFrame(graphics::ICommandContext& ctx, GpuTexture resolved_color, GpuTexture current_depth);
};
```

---

## 2. Graphics Hardware Abstraction Layer (HAL)

### `graphics::IGraphicsDevice` ([`graphics/abstraction/IGraphicsDevice.h`](../graphics/abstraction/IGraphicsDevice.h))
Factory interface creating textures, buffers, and command contexts:

```cpp
class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;
    virtual core::GraphicsApi GetApi() const noexcept = 0;
    virtual std::shared_ptr<IGraphicsTexture> CreateTexture(const core::TextureDesc& desc) = 0;
    virtual std::shared_ptr<IGraphicsTexture> OpenSharedTexture(uint64_t shared_handle) = 0;
    virtual std::shared_ptr<IGraphicsBuffer> CreateBuffer(const core::BufferDesc& desc, const void* initial_data) = 0;
    virtual std::shared_ptr<ICommandContext> GetImmediateContext() = 0;
};
```

### `graphics::ICommandContext` ([`graphics/abstraction/ICommandContext.h`](../graphics/abstraction/ICommandContext.h))
Hardware execution context dispatching compute passes and memory copies:

```cpp
class ICommandContext {
public:
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void SetComputeShader(void* native_shader) = 0;
    virtual void SetConstantBuffers(uint32_t slot, uint32_t count, IGraphicsBuffer* const* buffers) = 0;
    virtual void SetShaderResources(uint32_t slot, uint32_t count, IGraphicsTexture* const* textures) = 0;
    virtual void SetUnorderedAccessViews(uint32_t slot, uint32_t count, IGraphicsTexture* const* textures) = 0;
    virtual void CopyTexture(IGraphicsTexture* dst, IGraphicsTexture* src) = 0;
    virtual void Dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) = 0;
};
```

---

## 3. Upscaler & Reconstruction Interface

### `backends::IReconstructionBackend` ([`backends/reconstruction/IReconstructionBackend.h`](../backends/reconstruction/IReconstructionBackend.h))
Unified abstraction for DLSS, XeSS, FSR, and native OmniRender temporal accumulation:

```cpp
class IReconstructionBackend {
public:
    virtual ~IReconstructionBackend() = default;
    virtual std::string_view GetName() const noexcept = 0;
    virtual bool IsRuntimeAvailable() const noexcept = 0;
    virtual bool Initialize(graphics::IGraphicsDevice& device, const core::Resolution& in_res, const core::Resolution& out_res) = 0;
    virtual bool Execute(core::FrameContext& frame_ctx, graphics::ICommandContext& cmd_ctx) = 0;
    virtual void Shutdown() = 0;
};
```

---

## 4. Cross-Process IPC Protocol

### `OmniRenderIPCFrameData` ([`modules/common/ipc_protocol.h`](../modules/common/ipc_protocol.h))

```cpp
#pragma pack(push, 8)
struct OmniRenderIPCFrameData {
    uint32_t magic_header;          // 0x4F4D4E49 ("OMNI")
    uint32_t version;               // Protocol version
    uint64_t frame_index;           // Monotonically increasing frame index
    uint32_t surface_width;         // Internal game render resolution width
    uint32_t surface_height;        // Internal game render resolution height
    uint32_t target_width;          // Presentation resolution width
    uint32_t target_height;         // Presentation resolution height
    uint32_t color_format;          // DXGI_FORMAT enumeration
    uint32_t depth_format;          // DXGI_FORMAT enumeration
    uint64_t shared_color_handle;   // Fixed 64-bit GPU resource handle
    uint64_t shared_depth_handle;   // Fixed 64-bit GPU resource handle
    uint64_t shared_motion_handle;  // Fixed 64-bit GPU resource handle
    float    camera_near;           // Near clip plane
    float    camera_far;            // Far clip plane
    float    fov_vertical_rad;      // Field of view in radians
    uint32_t flags;                 // Bit 0: Reversed Z, Bit 1: Depth Inverted
    float    view_proj_current[16]; // Current frame view-projection matrix
    float    view_proj_previous[16];// Previous frame view-projection matrix
};
#pragma pack(pop)
```

---

## 5. Synchronization Primitives

| Name | Direction | Primitive Type | Purpose |
|---|---|---|---|
| `Global\OmniRender_FrameReady` | Hook → Daemon | Win32 Named Event / eventfd | Signals new frame posted to SPSC ring buffer |
| `Global\OmniRender_IPC_Block` | Hook ↔ Daemon | Memory-Mapped File / POSIX SHM | Backing storage for ring buffer slots |