# OmniRender v1.0.0 — Initial Release

First public release of OmniRender, the out-of-process neural
post-processing and temporal reconstruction engine for legacy 3D games.

>  This is a **scaffold release**. The hook vtable trampolines, NGX
> / TensorRT integrations, and the full DXGI shared-handle pipeline are
> stubbed; the build artifacts below contain a runnable, end-to-end
> skeleton that you can drop into a target game directory.

## Highlights

- **Modular monorepo layout** — `modules/{common, hook, daemon, shaders}`
  are independent CMake subprojects with their own READMEs.
- **Hook DLL** (`omnirender-hook.dll`) — D3D9 + DXGI present hooks,
  active depth heuristic, IPC ring buffer producer.
- **Daemon EXE** (`OmniRenderDaemon.exe`) — hardware negotiator, IPC
  server, OpenSharedResource ingestion, DIS optical flow, DLSS and
  spatial-fallback upscalers, TensorRT + compute tone map, borderless
  flip-model overlay.
- **Compute shaders** — `optical_flow_dis.hlsl`, `depth_linearize.hlsl`,
  `auto_hdr_tonemap.hlsl` (shader model `cs_6_5`).
- **MIT licensed** with `THIRD_PARTY_LICENSES.md` for ReShade, NVIDIA,
  Microsoft, AMD, and MinHook attributions.

## Downloads

| File | Description |
|------|-------------|
| `OmniRenderDaemon-windows-x64.zip`  | 64-bit daemon + hook DLL + compiled shaders + sample config |
| `Source-code.zip`                   | Full source tree (matches the `v1.0.0` tag) |
| `Source-code.tar.gz`                | Same, as tarball |

> NVIDIA proprietary runtimes (`nvngx_dlss.dll`, `sl.interposer.dll`,
> `sl.d3d11.dll`) are **not** redistributed. See the PRD §9 packaging
> section for install instructions.

## Quick start

```bat
:: Extract OmniRenderDaemon-windows-x64.zip into a target game folder.
:: Drop omnirender-hook.dll into the same folder (or as a ReShade add-on).

:: Launch the daemon (it will block, waiting for frames).
OmniRenderDaemon.exe

:: Launch the game as usual; the hook will publish frames to the daemon.
```

See [`docs/build.md`](https://github.com/GuruMachanica/OmniRender/blob/v1.0.0/docs/build.md)
and [`docs/architecture.md`](https://github.com/GuruMachanica/OmniRender/blob/v1.0.0/docs/architecture.md)
for the full pipeline walk-through.

## Verification status

| ID | Status |
|----|--------|
| QA-01 Address Space Safety | ⏳ scaffold — manual verification required |
| QA-02 Hardware Profile Gating | ⏳ scaffold — manual verification required |
| QA-03 Temporal Reconstruction | ⏳ scaffold — manual verification required |
| QA-04 Broad API Compatibility | ⏳ scaffold — manual verification required |

## Changelog

See [`CHANGELOG.md`](https://github.com/GuruMachanica/OmniRender/blob/v1.0.0/CHANGELOG.md).

## License

[MIT](https://github.com/GuruMachanica/OmniRender/blob/v1.0.0/LICENSE) —
see [`THIRD_PARTY_LICENSES.md`](https://github.com/GuruMachanica/OmniRender/blob/v1.0.0/THIRD_PARTY_LICENSES.md)
for third-party attributions.
