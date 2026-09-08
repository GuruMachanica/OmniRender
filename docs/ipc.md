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
#pragma pack(push, 1)
struct OmniRenderIPCFrameData {
    uint32_t magic_header;        // 0x4F4D4E49 ("OMNI")
    uint64_t frame_index;         // Monotonically increasing counter
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t target_width;
    uint32_t target_height;
    uint32_t color_format;        // DXGI_FORMAT
    uint32_t depth_format;        // DXGI_FORMAT
    HANDLE   shared_color_handle; // Win32 shared NT handle
    HANDLE   shared_depth_handle; // Win32 shared NT handle
    float    camera_near;
    float    camera_far;
    float    fov_vertical_rad;
    uint32_t flags;               // see IpcFlag
};
#pragma pack(pop)
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

## 5. Flag bits

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | `ReversedZ` | Hardware depth is reversed-Z (D3D/GL); daemon must invert before linearization. |
| 1   | `DepthRaw`  | Depth value is already inverted and must be passed through untouched. |

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