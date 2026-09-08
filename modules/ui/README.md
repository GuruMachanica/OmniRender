# `modules/ui` — OmniRender Control Center UI

The `modules/ui` directory contains the native Windows Control Center application (`OmniRenderControlPanel.exe`). It provides an intuitive, high-performance, dark-mode graphical user interface for launching games, configuring upscaler profiles (DLSS, FSR, XeSS), toggling Ray Tracing features, and monitoring the background rendering daemon.

---

## Subfolder Structure & Files

| File | Purpose |
|------|---------|
| [`control_panel.cpp`](./control_panel.cpp) | Win32 window creation, modern dark theme styling, control layout, settings persistence (`omnirender.ini`), and injection orchestration. |
| [`game_detector.h`](./game_detector.h) | Header interface for game detection, Steam library parsing, process scanning, and proxy DLL deployment. |
| [`game_detector.cpp`](./game_detector.cpp) | Implementation of Steam library discovery, active 3D process enumeration, and proxy injection logic. |
| [`CMakeLists.txt`](./CMakeLists.txt) | CMake target definition for `OmniRenderControlPanel` with native Windows subsystems. |

---

## Key Features

1. **Auto Game Detection**:
   - Automatically parses `libraryfolders.vdf` across all local drives to find installed Steam games.
   - Detects active 3D processes and games currently running on the system.
   - Includes a manual file browser (`Browse...`) for non-Steam or standalone executables.

2. **Upscaling & Reconstruction Selector**:
   - **Auto Detect**: Negotiates best upscaler based on GPU architecture and VRAM.
   - **AMD FSR 4.1 / 1.0**: EASU edge-adaptive spatial upscaling + RCAS sharpening.
   - **NVIDIA DLSS 5 / 3.x**: Tensor-core accelerated temporal reconstruction with Streamline/NGX interposer.
   - **Intel XeSS**: Cross-vendor DP4a and XMX machine-learning upscaling.
   - **Spatial CAS**: Contrast-Adaptive Sharpening fallback.

3. **Ray Tracing & Auto-HDR Toggles**:
   - Screen-Space Ray-Traced Reflections (SSR).
   - Ray-Traced Ambient Occlusion (RTAO).
   - Auto-HDR Inverse Tonemapping (DirectML / Neural).

4. **One-Click Orchestration**:
   - Copies proxy hooks (`d3d9.dll`, `dxgi.dll`, or `opengl32.dll`) to the target game directory.
   - Launches `OmniRenderDaemon.exe` in the background with zero terminal popups.
   - Launches the game process with crash-isolated out-of-process rendering.

---

## Building

The UI is built with MSVC and CMake using native Win32 APIs (no bulky external UI runtimes required):

```bat
cmake -B build -A x64 -DOMNIRENDER_BUILD_UI=ON
cmake --build build --config Release --target OmniRenderControlPanel
```

The output executable is placed at `build/bin/Release/OmniRenderControlPanel.exe`.

---

## Connected Documentation

- [Root Documentation](../../README.md)
- [Daemon Architecture](../daemon/README.md)
- [Hook Proxy Subsystem](../hook/README.md)
