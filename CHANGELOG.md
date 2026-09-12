# Changelog

All notable changes to OmniRender are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **v0.2.0-alpha is the new honest version.** `v1.0.0` was a
> misleading tag for what was actually a scaffold.

## [Unreleased]

### Added
- **Intel XeSS neural reconstruction backend for the runtime pipeline** —
  real `xessD3D11Init` + `xessD3D11Execute` through a dynamically loaded
  `libxess_dx11.dll` (`backends/reconstruction/xess/`), with the packed(8)
  XeSS ABI mirrored locally so no SDK headers are needed at build time.
  The backend consumes the runtime pipeline's linearized depth and
  pixel-space motion vectors, honors `pipeline.enable_xess` (off by
  default), and honestly reports unavailable on failure so selection
  falls through to FSR. Backend priority is now DLSS → XeSS → FSR →
  passthrough.
- **HUD pipeline status** — the overlay now shows the active backend
  ("NVIDIA DLSS" / "Intel XeSS" / "FSR 1.0 (EASU+RCAS)" / "Passthrough")
  and the live input → output resolution alongside the latency profiler.
- **FSR 1.0 spatial upscaling backend for the runtime pipeline** — the
  real EASU + RCAS compute dispatch (`backends/reconstruction/spatial/`)
  now runs when DLSS is unavailable, on every vendor, with no SDK. It
  allocates exactly two output-resolution textures (EASU target, RCAS
  target) plus two constant buffers — no full-res intermediates, bounded
  VRAM by construction. Backend priority is DLSS → FSR → passthrough.
- **Daemon-owned output resolution policy** (`renderer.output_scale`:
  `native` / `screen` / `quality` / `ultra` / `custom`). Hooks always
  published `target == surface`, which meant the upscale path could never
  engage; the daemon now decides the presented resolution itself and
  overrides `fc.output_resolution` per frame. Presentation re-sizes the
  overlay swapchain to the *upscaled* output (sticky across single-frame
  pipeline failures so the swapchain does not flap).

### Changed
- **Legacy XeSS adapter (`modules/daemon/upscalers/xess_adapter`)**
  rewritten from a guaranteed-fail stub to a real execute path (same
  mirrored ABI, per-resolution feature lifecycle, adapter-owned output
  texture instead of a leaking function-local static). Unit-test contract
  updated: without the runtime DLL the adapter must *reject* execution
  (never fabricate `DataSource::Reconstructed`).
- **Keyed-mutex ownership moved into `CaptureAdapter`** — the mutex on the
  imported color texture is acquired inside `Adapt()` (before any pipeline
  read) and released on the next `Adapt()`/destruction. The presentation
  loop no longer acquires it, removing both a double-acquire hazard and a
  read-before-acquire race introduced by processing frames before
  presentation sizing.
- FSR is enabled by default (`pipeline.enable_fsr = true`) since it is
  the only working vendor-neutral upscaler.
- **Depth linearization (`DepthProvider`)** — audit plan Commit 6, the last
  major temporal-pass gap. Raw game depth (non-linear NDC, possibly
  reversed-Z) is now converted to linearized [0,1] view depth before any
  consumer uses it:
  - New `core/temporal/DepthProvider` pass (GPU path via
    `DepthLinearize.cso`, CPU fallback via new
    `ICommandContext::ReadbackTexture` staging readback on D3D11).
  - `shaders/temporal/DepthLinearize.hlsl` handles standard- and reversed-Z
    (flag carried through `ReprojectionCB._pad`).
  - `runtime::Pipeline` runs it first in the graph; disocclusion and the
    previous-depth history now consume linearized depth (matching domains),
    while motion reprojection keeps consuming raw NDC depth for
    unprojection.
- **Canonical temporal shaders now compile.** `shaders/temporal/*.hlsl`
  (`MotionReproject`, `Disocclusion`, `ReactiveMask`, `DepthLinearize`) are
  compiled to CSO by the daemon shader step and copied next to the daemon,
  `gpu_test_host`, and `test_core_d3d11_execution`, so the core passes'
  optional GPU accelerators actually activate when DXC is installed (audit
  plan Commit 1 remainder).
- **OpenGL CPU-fallback pixel transport** — audit plan Commit 10. When
  `WGL_NV_DX_interop2` is unavailable, the GL hook now ships real pixels to
  the daemon instead of discarding the readback: a named shared file mapping
  (`Local\OmniRender_GL_Pixels_<pid>`) carries top-down RGBA8 frames, with
  new IPC fields (`pixel_block_name`, `pixel_data_size`, `pixel_row_pitch`,
  `IpcFlag::PixelDataCpu`, `struct_version 2`) and daemon-side mapping + GPU
  upload in `CaptureAdapter`. Frames with no usable color source are no
  longer published at all.
- **Proxy DLL artifacts.** The hook now emits drop-in `d3d9.dll`,
  `dxgi.dll`, and `opengl32.dll` copies (all proxy entry points already
  exported) and installs them under `bin/proxies/`.

### Fixed
- **Wrong DXGI format IDs in hook payloads.** D3D9/OpenGL hooks published
  `0x15` (=`R32_FLOAT_X8X24_TYPELESS`) as BGRA8 color and `0x29`/`0x22` as
  R32F/RG16F depth/motion; DXGI published `0x29`/`0x22` too. All now use the
  correct enum values (`87`, `41`, `34`) by name.
- **D3D9 depth staging was fabricated.** `StretchRect` from a D24S8/D16
  depth-stencil into an R32F render target is unsupported on D3D9; the old
  code copied anyway and the daemon consumed garbage depth. The hook now
  probes driver support once, copies depth only when it actually works, and
  otherwise publishes `DepthRaw` with a zero handle so the daemon never sees
  fake depth.
- **OpenGL flag collision.** The GL hook flagged zero-copy frames with
  `0x01`, which collides with `IpcFlag::ReversedZ` and made the daemon flip
  depth interpretation. Flags now come from the shared `IpcFlag` enum.
- **Dead code removed.** `modules/hook/depth_format.cpp`
  (`ExportDepthForFrame`) had no callers and has been deleted.
- **Daemon runtime path did not compile (Windows).** The five
  pipeline-unification commits left the new daemon sources unbuildable:
  - `dxgi_interceptor.cpp` re-defined globals that its own header already
    defines via `DXGI_DEFINE_GLOBALS` (C2086 x10).
  - `capture_adapter.cpp` / `pipeline_runtime.cpp` referenced
    `TextureFormat::RGBA8_UNORM` / `BGRA8_UNORM`, which do not exist; the
    canonical names are `R8G8B8A8_UNORM` / `B8G8R8A8_UNORM`.
  - `presentation_win.cpp` dereferenced `graphics::IGraphicsTexture*` without
    including its header (C2027).
  - `AttachBackend()` wrapped the daemon-legacy `DlssAdapter` (a *different*
    `IReconstructionBackend` type) in a non-owning `shared_ptr` that could not
    convert. It now creates the core-side
    `backends::dlss::DlssReconstructionBackend` — the real implementation of
    the interface `runtime::Pipeline` consumes — and reports its NGX state on
    init failure.
- **Ring-buffer producer/consumer race.** Slots now transition
  `Ready → Processing → Free` instead of reusing a legacy `Consumed`
  state. `ConsumeFrame()` moves a slot to `Processing` *before*
  returning it, and producers only reclaim `Free` slots, so the
  producer can no longer overwrite a slot while the consumer is
  still reading it (`modules/common/ring_buffer.h`,
  `modules/daemon/ipc_server.cpp`, all hook producers,
  `test_host.cpp`).
- **CPU motion-vector staging format.** The CPU reprojection fallback
  in `MotionReprojectionPass` now packs vectors as IEEE 754 binary16
  (`RG16F`) matching the `R16G16_FLOAT` UAV texture, and uploads with
  the correct row pitch (`width*4` bytes) instead of `width*8`.
  Previously floats were memcpy'd into an RG16F texture, producing
  garbage vectors whenever the GPU shader was unavailable.
- **Stale motion vectors on static cameras.** When the camera has no
  movement the pass zeroes the motion texture instead of reusing
  vectors from a previous (moving) frame.
- **CSO path resolution.** Temporal passes resolve shader blobs
  relative to the running executable (`core/temporal/ShaderPath.h`)
  rather than the process working directory.
- **Layout-matched GPU accelerators.** The core temporal passes load
  optional CSOs compiled from `shaders/temporal/*.hlsl` (their
  `ReprojectionCB` matches `core/temporal/TemporalPassBase.h`). They
  no longer reference the daemon-side `modules/shaders/*_hlsl.cso`
  blobs, whose constant buffers belong to the daemon D3D11 pipeline.
- **First-frame fallbacks.** CPU staging scratch for motion,
  disocclusion, and reactive passes is always allocated in
  `Initialize()`, so the uniform/neutral-mask CPU fallbacks never
  touch an empty buffer on the first frame.

### Changed
- **Resolution-agnostic reactive mask shader**
  (`shaders/temporal/ReactiveMask.hlsl`): history color is sampled
  with a normalized UV + linear sampler so current color and history
  may differ in resolution without coordinate mis-mapping.
- **Disocclusion pass decoupled from motion vectors.** The pass only
  compares current depth against reprojected previous depth, so
  `runtime/Pipeline.cpp` no longer declares `ReadMotion` as a
  prerequisite. Depth-only titles (no usable camera/motion data) now
  get disocclusion handling instead of silently skipping the pass.

## [0.7.0-alpha] - 2026-09-06

### Added
- **OpenGL capture path.** `modules/hook/opengl_interceptor.cpp`
  hooks `wglSwapBuffers` and `wglSwapLayerBuffers`. When
  `omnirender-hook.dll` is renamed to `opengl32.dll` and dropped
  into an OpenGL game's folder, the loader routes every
  opengl32!function through the proxy. The hook reads the
  backbuffer via `glReadPixels` (BGRA8) and publishes the same
  v0.4.0 IPC payload the D3D9 path uses.
- **PE-import-table renderer detection.**
  `modules/daemon/capability_matrix.{h,cpp}` walks the import
  table of any .exe and identifies the primary graphics API
  (D3D8/9/10/11/12/OpenGL/Vulkan). The launcher uses this to
  pick the right hook DLL.
- **Module-name detector.** `modules/hook/module_name.cpp`
  reports whether the DLL was loaded as `opengl32.dll` so the
  DllMain can install the wgl hook.
- **Test:** `tests/test_capability_matrix.cpp` validates the
  detector against the test binary itself.
- **Build:** the hook CMakeLists now emits an `opengl32.dll`
  copy of `omnirender-hook.dll` next to the binary, ready to
  be renamed in place.

### Notes
- OpenGL v0.7.0-alpha path does not yet share GPU resources
  with the daemon (no `WGL_NV_DX_interop`). Frames are
  published as CPU-side BGRA8 pixel data, which the daemon
  blit-passes through. v0.7.1-alpha adds the GL-NV-DX interop
  path so the daemon gets a shareable GPU handle.
- OpenGL core profile (4.x) context creation via
  `wglCreateContextAttribsARB` is not yet hooked. v0.7.1-alpha
  adds that too.

## [0.5.0-alpha] - 2026-09-06

### Added
- **TOML-style config loader.** `modules/common/config.{h,cpp}` reads
  a subset of TOML (sections, key/value, comments, quoted strings)
  sufficient for OmniRender's per-game profiles. Pure C++17, no
  third-party dependencies.
- **Layered config stack** (lowest to highest precedence):
  1. Built-in defaults
  2. Global config at `%USERPROFILE%\.omnirender\config.toml`
  3. Per-game profile at
     `%USERPROFILE%\.omnirender\profiles\<fnv64-of-exe>.toml`
  4. Environment variables with the `OMNIRENDER_` prefix
- **Per-game profile discovery.** The daemon hashes the running
  executable (FNV-1a 64-bit, dependency-free) and looks up the
  profile by hash. Same `.exe` always gets the same profile even
  if it moves.
- **First-run bootstrap.** When the daemon starts without a
  config file, it writes a default global config and a default
  per-game profile so the user has a starting point to edit.
- **Per-game profile sample** at
  `docs/profiles/skyrim.toml` (the audit-point #23 schema).
- **`docs/configuration.md`** documenting the schema, the
  precedence rules, and the env-var mapping.
- **`tests/test_config.cpp`** covering the parser, the merge
  order, env-var override, the file hash, the round-trip
  save/load, and the default profile.
- **`modules/daemon/main.cpp`** rewritten to use the layered
  config instead of the `std::getenv` block.

### Changed
- **Version bumped** from 0.4.0-alpha to 0.5.0-alpha.
- **Config knobs** moved from `std::getenv` calls in `main.cpp`
  to the layered config. The env-var names are unchanged, so
  existing `OMNIRENDER_*` environment variables still work.

## [0.4.0-alpha] - 2026-09-06

### Added
- **DXC compile pipeline.** The daemon CMakeLists now invokes `dxc`
  to compile every shader in `modules/shaders/` to CSO at build
  time, copies the CSOs next to the daemon binary, and ships them
  in the install step. `OMNIRENDER_BUILD_CSO=OFF` skips the compile
  if DXC isn't installed.
- **Depth + view-projection reprojection motion vectors.**
  `modules/shaders/motion_reproject_hlsl.hlsl` computes per-pixel
  2D motion from current/previous view*proj + linearized depth.
  Wired in via `processing::BuildMotionVectors`. This is the
  primary motion source; the older `optical_flow_dis.hlsl` is now
  a fallback.
- **Reactive mask.** `modules/shaders/reactive_mask_hlsl.hlsl`
  identifies pixels where the historical color sample should not
  be trusted (particles, fire, UI, transparency, foliage).
  Conservative thresholds. Wired in via
  `processing::BuildReactiveMask`.
- **Halton 2,3 jitter.** `modules/common/halton.h` produces a
  period-16 sub-pixel jitter that converges in 4 frames. The
  D3D9 and DXGI hooks now stamp `jitter_x` / `jitter_y` on every
  payload so the daemon can undo the jitter at composite time.
- **IPC struct v1.** `OmniRenderIPCFrameData::struct_version`
  lets the daemon reject v0.3.0 payloads from older hooks. The
  v0.4.0 fields are: `view_proj_current[16]`,
  `view_proj_previous[16]`, `jitter_x`, `jitter_y`,
  `shared_motion_handle`, `motion_format`.
- **Two new unit tests:** `test_cso_compile` (verifies CSO
  artifacts exist in the build tree) and `test_halton` (verifies
  the jitter sequence).

### Changed
- **Version bumped** from 0.3.0-alpha to 0.4.0-alpha.
- **D3D9 hook** now captures `GetTransform(D3DTS_VIEW)` and
  `GetTransform(D3DTS_PROJECTION)` per frame, multiplies them,
  and stores the column-major view*proj in the IPC payload.
- **Pipeline** calls `BuildMotionVectors` and `BuildReactiveMask`
  before the existing reconstruction pass so future consumers
  can blend history using the disagreement mask.
- **DXGI hook** stamps `struct_version` and zero matrices so the
  daemon knows it's a v0.4.0 payload even without a captured
  camera.

### Fixed
- `ipc_protocol.h` was missing the `view_proj_*` and
  `jitter_*` fields; the daemon would have rejected any frame
  the v0.4.0 hook published.
- `processing.cpp` had no `BuildMotionVectors` /
  `BuildReactiveMask` symbols; the pipeline called them and
  would have failed to link.

## [0.3.0-alpha] - 2026-09-06

### Added
- **Bounded working resolution** — `kMaxProcessingWidth=1920`,
  `kMaxProcessingHeight=1200`. Source frames larger than this are
  clamped so the daemon never allocates more than ~180 MB of
  scratch VRAM.
- **3-frame ping-pong history** for the future temporal
  reconstruction pass.
- **Configuration knobs** in `modules/common/pipeline_config.h`:
  enable/disable reconstruction, upscale, tonemap, optical flow,
  DLSS, FSR, XeSS, RT effects, plus per-axis resolution caps and
  history depth. Overridable via `OMNIRENDER_ENABLE_*` /
  `OMNIRENDER_MAX_*` environment variables.
- **Real passthrough blit pixel shader** (`modules/shaders/passthrough_ps.hlsl`)
  for the overlay presentation path. Replaces the v0.2.0-alpha debug
  shader.
- **First compute shaders** in `modules/shaders/`:
  `upscale_hlsl.hlsl` (Lanczos-3 bicubic), `reconstruct_hlsl.hlsl`
  (disagreement-masked temporal blend), `depth_linearize_hlsl.hlsl`
  (reverse-Z / log depth flatten).
- **In-process test renderer** (`modules/daemon/test_host.cpp`)
  publishes 600 synthetic frames at 60 Hz through the same IPC
  contract the hook uses. Enable with
  `-DOMNIRENDER_BUILD_TEST_HOST=ON`.
- **Improved depth format detection** in
  `modules/hook/depth_format.cpp`. Records the real D3D9 depth
  format in the IPC payload and sets the `DepthRaw` flag.
- **File logger with rotation** (`modules/common/log_file.h`,
  10 MB × 5 files) activated by `OMNIRENDER_LOG_FILE`.
- **Module headers extracted** to make the public API explicit:
  `interop_d3d11.h`, `ipc_server.h`, `presentation_win.h`,
  `processing.h`, `pipeline.h`, `tonemap_neural.h`.
- **Audit backlog** in `docs/AUDIT_TODO.md` tracking every point
  raised in the architectural review.
- **Build option** `-DOMNIRENDER_BUILD_TEST_HOST=ON` for the test
  renderer.
- **Pipeline v0.3 docs**: `docs/pipeline_v0.3.md`,
  `docs/roadmap_v0.3.md`, `docs/3rd_party_deps.md`.

### Changed
- **Version bumped** from 0.2.0-alpha to 0.3.0-alpha.
- **Daemon CMake** no longer requires DXC at configure time; the
  pre-build shader compile is left for `0.3.1-alpha` once the HLSL
  converges.
- **`tonemap_neural.cpp`** renamed its entry point to
  `DispatchNeuralTonemap` to disambiguate from the compute
  `DispatchTonemap` in `processing.cpp`.
- **Removed** the duplicate `tonemap_compute.{cpp,h}` and
  `modules/daemon/shaders/` directory. All compute shaders live
  canonically under `modules/shaders/`.
- **`presentation_win.cpp`** now uses public accessors
  (`Device()`, `Context()`, `SwapChain()`, `RenderTargetView()`)
  instead of file-static globals, so `pipeline.cpp` can use them.

### Fixed
- `processing.cpp` namespace structure (the v0.2.0-alpha scaffold
  had a malformed nested `namespace omnirender::daemon` block).
- `pipeline.cpp` references to file-static `g_swapchain` / `g_rtv`
  now use the public accessors.
- `passthrough_ps.hlsl` is now a real fullscreen blit (was a debug
  shader returning `uv` colors in v0.2.0-alpha).
- `Dockerfile` empty line in the `RUN powershell` block removed
  (replaced with a direct CMake build in the same step).

## [0.2.0-alpha] - 2026-09-06

### Added
- **Real vtable hook** for `IDirect3DDevice9` (Present, EndScene,
  Reset, SetDepthStencilSurface) and `IDXGISwapChain::Present` via
  the shared `modules/common/vtable_hook.h` helper.
- **Active depth capture** with format-aware selection (D16, D24X8,
  D24S8, D24FS8, D32, D32F_LOCKABLE, D15S1, D16_LOCKABLE) and a
  shareable R32F / A8R8G8B8 staging surface.
- **SPSC ring buffer state machine** in `modules/common/ring_buffer.h`
  with explicit `FREE / CAPTURED / READY / CONSUMED` slot states. The
  producer can no longer silently overwrite an unprocessed slot.
- **GPU fence helper** in `modules/common/shared_fence.h` that
  blocks the consumer until the slot's fence value reaches the
  frame index advertised in the IPC payload.
- **Daemon per-frame blit pipeline** in `presentation_win.cpp`:
  ConsumeFrame → FenceWait → OpenSharedResource → Blit → Present.
  Native-resolution passthrough, no processing.
- **First unit test** (`tests/test_ring_buffer.cpp`) covering the
  SPSC state machine. 1000 frames, two threads, zero overwrites.

### Changed
- **Version bumped** from `1.0.0` to `0.2.0-alpha`. The previous
  `1.0.0` tag was a scaffold, not a product.
- **Capability matrix** (audit point #14) deferred to `0.5.0-alpha`.
- **OpenGL / Vulkan capture** deferred to `0.7.0-alpha`.
- **DLSS, FSR 2/3, XeSS** deferred to `0.5.0-alpha`+.
- **Neural inverse tone mapping** kept as a core feature, target
  `0.3.0-alpha`. The audit recommended dropping it; I disagree
  because it is the highest-visual-impact-per-line feature we have.
- **Watchdog behaviour** deliberately does NOT restart the daemon
  from the hook (security boundary).
- **Logger** still basic; file logging and rotation are `0.4.0`
  work.

### Fixed
- IPC ring no longer has a silent over-write window.
- Hook vtable hooks are no longer stubs.
- Shared-resource lifetime is now fenced (partial).
- Daemon actually loops.

## [0.1.0-scaffold] - 2026-09-06

### Added
- Initial module layout, IPC contract, IPC ring, hardware negotiator,
  HLSL compute shaders, MIT license, CI workflow, GHCR publish
  workflow, draft release notes.

>  Renamed to `0.1.0-scaffold` retroactively; it was tagged
> `1.0.0` which was inaccurate.
