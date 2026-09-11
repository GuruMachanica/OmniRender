# Inter-Process Contract

The OmniRender hook and daemon communicate **only** through a small
set of OS primitives. This contract is stable; any change here
requires coordinated updates to **both**
[`modules/hook`](../modules/hook/) and
[`modules/daemon`](../modules/daemon/).

## 1. Named file mapping — `Global\OmniRender_IPC_Block`

The shared memory backing the ring buffer. Created with
`CreateFileMappingA(INVALID_HANDLE_VALUE, …, PAGE_READWRITE)`.

### Layout

```
offset 0           : RingHeader { producer_index, consumer_index, magic, capacity }
offset sizeof(...) : OmniRenderIPCFrameData[capacity]
```

`producer_index` and `consumer_index` are `volatile LONG`. They are
manipulated with `_InterlockedExchangeAdd` (release-acquire semantics
on x86/x64 Windows). The current implementation uses
`capacity = 4` frames.

## 2. Named event — `Global\OmniRender_FrameReady`

Auto-reset event, set by the hook after every `PublishFrame`. The
daemon blocks on it with `WaitForMultipleObjects`. This is the
**frame-ready signal** — the only wakeup source for the daemon's
consumer loop.

## 3. `OmniRenderIPCFrameData` struct

Defined in
[`modules/common/ipc_protocol.h`](../modules/common/ipc_protocol.h).
Byte-for-byte identical to PRD §5.1.

```cpp
// Defined in modules/common/ipc_protocol.h (pack(8)). struct_version:
//   0 = v0.3.0 layout, 1 = v0.4.0 (VP/jitter/motion), 2 = v0.5.0 (pixel block)
struct OmniRenderIPCFrameData {
    uint32_t magic_header;         // 0x4F4D4E49 ("OMNI")
    uint32_t struct_version;
    uint64_t frame_index;          // Monotonically increasing counter
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t target_width;
    uint32_t target_height;
    uint32_t color_format;         // DXGI_FORMAT value
    uint32_t depth_format;         // DXGI_FORMAT value (0 = none)
    uint64_t shared_color_handle;  // fixed-width cross-process handle
    uint64_t shared_depth_handle;
    uint64_t shared_motion_handle;
    float    camera_near;
    float    camera_far;
    float    fov_vertical_rad;
    float    jitter_x;             // Halton(2) sub-pixel offset (v0.4.0)
    float    jitter_y;             // Halton(3) sub-pixel offset (v0.4.0)
    float    view_proj_current[16];  // view*proj of this frame (v0.4.0)
    float    view_proj_previous[16]; // view*proj of last frame (v0.4.0)
    uint32_t motion_format;          // DXGI_FORMAT_R16G16_FLOAT (v0.4.0)
    uint32_t flags;                  // see IpcFlag
    // v0.5.0: CPU pixel fallback channel (OpenGL without WGL_NV_DX_interop2)
    char     pixel_block_name[48];   // named file mapping, empty = none
    uint32_t pixel_data_size;        // total bytes in the mapping
    uint32_t pixel_row_pitch;        // bytes per row (width*4 for RGBA8)
};
```

## 4. DXGI shared NT handles

The hook calls `IDXGIResource::GetSharedHandle` (or
`IDXGIResource::CreateSharedHandle` on D3D11.1+) on the backbuffer
**and** the active depth surface, and writes the resulting `HANDLE`
into the `shared_*_handle` fields of the frame data. The daemon
opens them with `ID3D11Device::OpenSharedResource` — **zero PCIe
copy**.

>  DirectX 9 surfaces cannot be directly shared. The hook first
> copies them into a `D3DPOOL_DEFAULT` A8R8G8B8 surface with a
> `pSharedHandle` parameter, then exports that handle. See
> [`modules/hook/d3d9_interceptor.cpp`](../modules/hook/d3d9_interceptor.cpp).

### CPU pixel fallback channel (OpenGL, IPC v2+)

When a game uses OpenGL and the driver does not expose
`WGL_NV_DX_interop2`, there is no GPU shared handle. The GL hook then
publishes color through a plain shared file mapping:

```text
wglSwapBuffers (no interop)
  glReadPixels (RGBA8, bottom-up)
  flip rows top-down
  copy into Local\OmniRender_GL_Pixels_<pid>
  publish payload: pixel_block_name / pixel_data_size / pixel_row_pitch
                   flags |= IpcFlag::PixelDataCpu, color_format = 28 (R8G8B8A8)

daemon (CaptureAdapter)
  OpenFileMappingA(FILE_MAP_READ)
  UploadTextureData into an owned R8G8B8A8 texture
  FrameContext.color = that texture
```

This path costs one CPU round-trip per frame and is bounded to a single
input-resolution image; it exists so OpenGL games on hardware without
interop still get the full daemon pipeline instead of dropping frames.

## 5. Flag bits

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | `ReversedZ` | Hardware depth is reversed-Z (D3D/GL); daemon must invert before linearization. |
| 1   | `DepthRaw`  | Depth was not acquired / is unpopulated; `shared_depth_handle` is not valid. |
| 2   | `CameraZero` | `view_proj_current`/`view_proj_previous` are all-zero (camera not extracted). |
| 11  | `PixelDataCpu` | Color arrives as CPU pixels in `pixel_block_name` (no GPU shared handle). IPC v2+ only. |

## 6. Lifecycle

```text
hook                                  daemon
----                                  ------
CreateFileMappingA(name)              OpenFileMappingA(name)
CreateEventA(name)                    OpenEventA(name)
                                      Initialize D3D11 device
loop:
  Present()
  GetSharedHandle(color)
  GetSharedHandle(depth)
  RingHeader.slot = producer_index % 4
  slots[slot] = frame
  producer_index++
  SetEvent(FrameReady)                 WaitForMultipleObjects
                                         OpenSharedResource(color, depth)
                                         Run pipeline
                                         Ack via consumer_index++
                                      loop
```

## 7. Failure modes

| Symptom | Likely cause | Hook response | Daemon response |
|---------|--------------|---------------|-----------------|
| `OpenFileMappingA` returns null | Daemon not yet running | Create the mapping itself (it'll be reused when daemon starts) | Open a fresh mapping |
| `OpenEventA` returns null | Daemon not yet running | `PublishFrame` early-returns | `ConsumeFrame` early-returns |
| Daemon process dies | Crash, OOM, TDR | `OpenEventA` returns null on next attempt → no-op | n/a |
| `OpenSharedResource` fails (HR != S_OK) | Stale handle, surface lost | Re-acquire handle next frame | Log + skip frame, retry |