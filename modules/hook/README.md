# `modules/hook` — Client Injected Proxy DLL

The `omnirender-hook` module is the **lightweight, in-process** half
of OmniRender. It runs inside the legacy game process, captures the
backbuffer and depth/stencil surfaces, exports them as DXGI shared
NT handles, and writes per-frame metadata into the shared memory ring
buffer.

> **Working-set budget:** 50 MB (PRD FR-1.2). The CMake target links
> with `/OPT:NOREF /OPT:NOICF` to keep code small.

## Files

| File | Role |
|------|------|
| [`main.cpp`](./main.cpp) | `DllMain` entry point. Installs all interceptors. |
| [`d3d9_interceptor.cpp`](./d3d9_interceptor.cpp) | D3D9 vtable hook. Decomposed to adhere strictly to $\le$ 300 LOC. |
| [`d3d9_shared_surfaces.cpp`](./d3d9_shared_surfaces.cpp) / [`.h`](./d3d9_shared_surfaces.h) | D3D9 `D3DPOOL_DEFAULT` shared surface allocation, texture mapping, and cross-process NT handle management. |
| [`dxgi_interceptor.cpp`](./dxgi_interceptor.cpp) | Present hook for D3D10/11/12. Copies the backbuffer into a shared D3D11 texture and exports its handle. |
| [`opengl_interceptor.cpp`](./opengl_interceptor.cpp) | OpenGL / WGL `wglSwapBuffers` interceptor. Dispatches zero-copy GPU capture or `glReadPixels` fallback. |
| [`opengl_interop.cpp`](./opengl_interop.cpp) / [`.h`](./opengl_interop.h) | Zero-copy OpenGL <-> Direct3D 11 GPU texture bridge using `WGL_NV_DX_interop2`. Eliminates host CPU readbacks. |
| [`depth_locator.cpp`](./depth_locator.cpp) | Heuristic active-depth/stencil finder (matches backbuffer size, D24S8/D32 family, currently bound). |
| [`ipc_client.cpp`](./ipc_client.cpp) | Ring-buffer producer. Single-producer / single-consumer (SPSC) with explicit slot state. |
| [`module_name.cpp`](./module_name.cpp) | Process name and loaded module detection utilities. |

## Injection Paths

The DLL is **injection-method agnostic**. Three are supported:

1. **ReShade Add-on** — drop-in `omnirender-hook.addon` next to the
   ReShade DLL; ReShade's add-on loader calls our `DllMain`.
2. **DXVK / Win32 proxy DLL** — rename to `d3d9.dll` or `dxgi.dll` and
   place alongside the game executable; Windows loads it via the
   standard DLL search order.
3. **Manual injector** — any x86/x64 injector that calls
   `LoadLibraryW(L"omnirender-hook.dll")`.

## Public surface

This module **exports nothing**. Its entire API is the IPC ring buffer
and the shared NT handles documented in
[`docs/ipc.md`](../../docs/ipc.md).

## Building

```bat
cmake -B build -A x64 -DOMNIRENDER_BUILD_HOOK=ON
cmake --build build --config Release --target omnirender-hook
```

Output: `build\modules\hook\Release\omnirender-hook.dll`

## Dependencies

| Library | Why |
|---------|-----|
| `d3d9` | D3D9 backbuffer + staging surface creation |
| `d3d11` | Used for shared handle creation on D3D10/11 paths |
| `dxgi`, `dxguid` | `IDXGISwapChain`, `IDXGIResource` |
| `omnirender::common` | Logger + IPC struct |

## Memory budget

- Allocations are bounded: one shared color surface + one shared depth
  surface per session. No per-frame heap traffic.
- `OMNI_LOG_*` calls go to `stderr` (debugger attached) **or** the
  Windows debugger via `OutputDebugStringA` only — no file I/O.

## Capture status

- D3D9 and DXGI capture paths are implemented and carry frames through
  the IPC ring to the daemon.
- Depth capture is implemented, but the daemon still linearizes it in a
  shader later. The hook now records the real source depth format and
  linearization hints so the daemon can do that correctly.
- Reconstruction, upscale, DLSS, FSR, XeSS and neural tonemap are still
  deferred to later releases.

## See also

- [Central Repository README](../../README.md)
- [`docs/architecture.md`](../../docs/architecture.md) — process topology
- [`docs/api_reference.md`](../../docs/api_reference.md) — IPC contract
- [`docs/pipeline_v0.3.md`](../../docs/pipeline_v0.3.md) — daemon-side pipeline
- [`docs/qa.md`](../../docs/qa.md) — acceptance criteria for the hook