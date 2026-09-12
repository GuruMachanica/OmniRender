# Audit Backlog

Status of every point raised by the architectural audit. Each item is
either:

- **Done** — landed in v0.3.0-alpha.
- ⏳ **In progress** — partial work in tree, finishes in a later version.
- ⏭ **Deferred** — explicitly scheduled, with a target version.

## Architecture / process (audit §2, §4, §5, §46)

- Modular `modules/` layout with per-module CMake + README.
- SPSC ring state machine with `FREE / CAPTURED / READY / CONSUMED`.
- GPU fence wait between producer and consumer.
- ⏭ Capability matrix (vendor × VRAM × feature level × driver version) — `0.5.0-alpha`.

## Hook & capture (audit §3, §11, §20, §22)

- Real D3D9 vtable hook: `Present`, `EndScene`, `Reset`, `SetDepthStencilSurface`.
- Real DXGI vtable hook: `IDXGISwapChain::Present` (lazy install).
- Active depth capture with format-aware selection.
- **OpenGL** capture — landed in v0.7.0-alpha. `opengl32.dll` proxy with `wglSwapBuffers` hook + `glReadPixels` backbuffer capture.
- ⏭ **D3D8** support via `d3d8to9` / `dgVoodoo2` — `0.7.0-alpha`.
- ⏭ **Reactive mask** (particles, UI, transparency, fire, smoke, foliage) — `0.4.0-alpha`.
- ⏭ **Disocclusion mask** — `0.4.0-alpha`.

## Reconstruction (audit §7, §20, §21)

- Bounded working resolution, history ping-pong, work UAV, constants buffer.
- Compute-shader linearize / reconstruct / tonemap entry points.
- **Depth + camera reprojection** motion vectors — landed in v0.4.0-alpha. Use depth + camera intrinsics before falling back to optical flow (audit #20).
- **Halton 2,3 jitter** sequence — landed in v0.4.0-alpha.
- ⏭ **Optical flow** (NVIDIA Optical Flow SDK or DIS compute) — `0.5.0-alpha`.

## Upscaling (audit §6, §7)

- ✅ **DLSS** via NGX — landed (`backends/reconstruction/dlss/`, real `NVSDK_NGX` evaluation with registry-based loader).
- ✅ **XeSS** via `libxess_dx11.dll` — landed (`backends/reconstruction/xess/`, mirrored packed ABI, real `xessD3D11Init` + `xessD3D11Execute`, no SDK headers needed).
- ✅ **FSR 1.0 (EASU + RCAS)** — landed (`backends/reconstruction/spatial/`, vendor-neutral fallback, on by default).
- ⏭ **FSR 2/3** temporal upscaling via FidelityFX SDK — `0.5.0-alpha`.

## Tone mapping (audit §9)

- Compute-shader auto-HDR tonemap in `processing.cpp` (uses linearize shader as placeholder for v0.3.0).
- **Neural inverse tone mapping** (TensorRT FP16) — landed in v0.4.0-alpha.
- Disagreement with audit #9: we keep this as a core feature because it has the highest visual impact per line of code.

## Presentation (audit §10)

- Borderless flip-model overlay window.
- DXGI swap chain recreate on resolution change.
- ⏭ Real upscale target swap chain (currently 1:1 passthrough scale) — `0.3.1-alpha`.

## IPC & integration (audit §4, §30, §31, §32)

- State-machine ring buffer.
- GPU fence.
- ⏭ **Per-slot explicit state fields** with `fence` value (we have fence, not full state machine) — already done in v0.2.0-alpha.
- ⏭ **Per-user session named objects** instead of `Global\` — `0.6.0-alpha`. Audit #32's security recommendation; lower priority than the rendering features.
- ⏭ **Watchdog**: hook detects daemon death via `OpenEventA` failure and disables itself cleanly. The audit recommended restart-from-hook; we deliberately do **not** do that (security boundary).

## Logging (audit §33)

- Thread-safe logger with `OMNI_LOG_*` macros.
- ⏭ **File logging + rotation** (10 MB × 5 files) — `0.4.0-alpha`.
- ⏭ Per-subsystem log tags (`IPC`, `HOOK`, `GPU`, `PERF`, `IPC`) — `0.4.0-alpha`.

## Configuration (audit §23, §28, §29)

- `pipeline_config.h` with env-var overrides.
- ⏭ **TOML config** under `~/.omnirender/config.toml` and per-game overrides — `0.5.0-alpha`.
- ⏭ **Automatic profile discovery** by executable hash — `0.5.0-alpha`.

## Tooling (audit §24, §25, §26, §27, §36, §37, §38)

- ⏭ **OmniRender Control Center** GUI — post-`1.0.0`.
- ⏭ **In-game overlay** (FPS, frametime, internal resolution, upscaler, depth/motion status) — post-`1.0.0`.
- ⏭ **Benchmark mode** (`F11` capture, PSNR/SSIM/LPIPS comparison) — post-`1.0.0`.
- ⏭ **Compatibility database** — post-`1.0.0`.
- ⏭ **TestHost** deterministic test renderer — `0.3.1-alpha` (stub landed in v0.3.0-alpha).
- ⏭ **Automated image-quality CI** — post-`1.0.0`.
- ⏭ **Frame debugger mode** (`F12` raw color / depth / motion / jitter / history / reactive / upscaled / final) — post-`1.0.0`.

## Branding (audit §39, §40, §41, §42, §43, §44, §45)

- ⏭ Logo + wordmark + monogram — post-`1.0.0`.
- ⏭ Visual identity (palette, typography) — post-`1.0.0`.
- ⏭ Logo variants (full, icon, mono, GitHub avatar, app icon) — post-`1.0.0`.
- ⏭ Website `omnirender.dev` — post-`1.0.0`.
- ⏭ Visual README (hero screenshot, before/after, supported games table) — post-`1.0.0`.
- ⏭ Positioning statement: *"Modern reconstruction and upscaling for legacy games."* — post-`1.0.0`.

## CI / QA (audit §35, §36, §37, §38)

- CI builds the daemon and the ring-buffer test.
- ⏭ **60-minute soak test** (QA-01) — `0.3.1-alpha`.
- ⏭ **GPU runners** (NVIDIA, AMD, Intel) — post-`1.0.0`.
- ⏭ **Shader compilation test** in CI — `0.3.1-alpha`.
- ⏭ **Integration tests** with the TestHost — `0.3.1-alpha`.

## Build (audit §34)

- `find_package` / proper SDK location for the optional NGX/TRT libraries.
- ⏭ **Pre-built CSO pipeline** (compile `modules/shaders/*.hlsl` to `.cso` at build time) — `0.3.1-alpha`. We removed the daemon's `find_program(DXC)` block in v0.3.0 because the v0.3.0 HLSL is still being iterated on; the stable pipeline lands in `0.3.1-alpha`.

## Out of scope (post-`1.0.0`)

- Manager UI
- Per-game profile database
- Public website
- Logo work

## Versioning note

We are **not** bumping to a fake 1.0.0. v0.3.0-alpha is the current
honest version. Bumps will follow the roadmap in
[`docs/roadmap_v0.3.md`](./roadmap_v0.3.md).
