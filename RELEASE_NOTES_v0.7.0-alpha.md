# OmniRender v0.7.0-alpha — OpenGL capture + PE-import detection

This release adds **OpenGL capture** via an `opengl32.dll` proxy
and a **PE import-table inspector** that detects the host
executable's primary graphics API at launch time.

## What works

- **OpenGL capture.** `modules/hook/opengl_interceptor.cpp`
  hooks `wglSwapBuffers` and `wglSwapLayerBuffers`. When
  `omnirender-hook.dll` is renamed to `opengl32.dll` and dropped
  next to an OpenGL game, the Windows loader routes every
  opengl32!function through the proxy. The hook reads the
  backbuffer via `glReadPixels` (BGRA8) and publishes the same
  v0.4.0 IPC payload the D3D9 path uses.
- **PE-import-table renderer detection.**
  `modules/daemon/capability_matrix.{h,cpp}` walks the .exe's
  import table (DOS header → NT headers → optional header → import
  directory → `IMAGE_IMPORT_DESCRIPTOR` array) and identifies
  the primary graphics API. The launcher uses this to pick the
  right hook DLL.
- **Module-name detector.** `modules/hook/module_name.cpp`
  reports whether the hook DLL is being loaded as `opengl32.dll`
  (the OpenGL proxy rename) so the DllMain can install the wgl
  hook.
- **New test:** `tests/test_capability_matrix.cpp` validates
  the detector against the test binary itself.
- **Build:** the hook CMakeLists emits an `opengl32.dll`
  copy of `omnirender-hook.dll` next to the binary, ready to be
  renamed in place.

## What is NOT in this release

| Feature | Status | Target |
|---------|--------|--------|
| **D3D8 native capture** | Not implemented (use d3d8to9 wrapper) | 0.7.1-alpha |
| **GL-NV-DX interop** (zero-copy GPU path) | Not implemented (BGRA8 blit-only) | 0.7.1-alpha |
| **OpenGL core profile** (`wglCreateContextAttribsARB`) | Not hooked | 0.7.1-alpha |
| DLSS / FSR / XeSS | Not implemented | 0.6.0-alpha+ |
| Optical flow (fallback) | Not implemented | 0.6.0-alpha+ |
| 60-min soak test | Not implemented | 0.5.1-alpha |
| Disocclusion mask | Not implemented | 0.5.1-alpha |

## How to use the OpenGL capture

```bat
:: Build the hook with OpenGL support (on by default):
cmake -B build -A x64 -DOMNIRENDER_BUILD_HOOK=ON
cmake --build build --config Release

:: In the build output you now have:
::   build\modules\hook\Release\omnirender-hook.dll
::   build\modules\hook\Release\omnirender-hook.dll.opengl32.dll

:: To enable OpenGL capture for a specific game:
::   1. Copy omnirender-hook.dll.opengl32.dll into the game folder.
::   2. Rename it to opengl32.dll.
::   3. Drop the daemon next to it.
::   4. Launch the game. The daemon's overlay window shows the frames.
```

The launcher's `InspectExecutable` reads the .exe's import table
and tells you which API the game uses:

```
[OmniRender daemon] renderer detect: primary=OpenGL imported_dlls=14
[OmniRender daemon]   - D3D11
[OmniRender daemon]   - D3D9
[OmniRender daemon]   - OpenGL
```

## Build

```bat
git clone https://github.com/GuruMachanica/OmniRender.git
cd OmniRender
git checkout v0.7.0-alpha
cmake -B build -A x64 ^
    -DOMNIRENDER_BUILD_TESTS=ON ^
    -DOMNIRENDER_BUILD_TEST_HOST=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Compatibility

| Renderer | v0.7.0-alpha | Notes |
|----------|--------------|-------|
| **D3D9** |  Fully supported | v0.2.0-alpha onward |
| **D3D11 / DXGI** |  Fully supported | v0.2.0-alpha onward |
| **OpenGL 1.5–3.x compatibility** |  Capture only (BGRA8 blit) | v0.7.0-alpha |
| **OpenGL 4.x core profile** |  Partial — wglCreateContextAttribsARB not yet hooked | 0.7.1-alpha |
| **D3D8** |  Use d3d8to9 wrapper, then D3D9 path | 0.7.1-alpha |
| **D3D10** |  Same as D3D11 (DXGI path) | v0.4.0-alpha onward |
| **D3D12** |  Not yet | post-1.0.0 |
| **Vulkan** |  Not yet | post-1.0.0 |

## Roadmap

| Version | Focus |
|---------|-------|
| 0.7.0-alpha | This release — OpenGL capture + PE-import detection |
| 0.7.1-alpha | GL-NV-DX interop (zero-copy path), OpenGL core profile hook, D3D8 via d3d8to9 |
| 0.8.0-alpha | Optical flow fallback for motion vectors |
| 0.9.0-rc    | Release candidate |
| 1.0.0       | Stable, QA-01..04 verified |

## License

[MIT](https://github.com/GuruMachanica/OmniRender/blob/v0.7.0-alpha/LICENSE).
