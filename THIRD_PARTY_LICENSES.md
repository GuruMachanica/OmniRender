# Third-Party Notices & Attributions

OmniRender is licensed under the **GNU General Public License v3.0 (GPLv3)** (see [`LICENSE`](./LICENSE)).
It interfaces with several external libraries and graphics runtimes. Their
respective licenses are summarised below; the upstream project is the
authoritative source.

## 1. ReShade Add-on SDK
- **License:** BSD 3-Clause
- **Copyright (c)** Patrick Mours. All rights reserved.
- **Source:** https://github.com/crosire/reshade
- Used for the optional ReShade Add-on injection path that loads
  `omnirender-hook.dll` into the legacy game process.

## 2. NVIDIA Streamline & NGX SDK
- **Streamline framework headers** are used under their open-source terms
  (MIT-style permissive). Source:
  https://github.com/NVIDIAGameWorks/Streamline
- **Proprietary runtimes** (e.g. `nvngx_dlss.dll`, `sl.interposer.dll`,
  `sl.d3d11.dll`) are the property of NVIDIA Corporation and are subject
  to the **NVIDIA Developer SDK License Agreement**. They are **not**
  redistributed in this repository. The daemon dynamically loads them at
  runtime when present on the host system.

## 3. DirectX Shader Compiler (DXC) and DirectX Headers
- **License:** MIT License / Apache 2.0 with LLVM Exception
- **Copyright (c)** Microsoft Corporation.
- DXC is invoked at build time to compile the HLSL compute shaders in
  `modules/shaders/`. The compiled DXIL blobs are not committed to the
  repository.

## 4. MinHook (optional)
- **License:** BSD 2-Clause
- **Copyright (c)** 2009-2016 Tsuda Kageyu. All rights reserved.
- Used as the in-process vtable trampoline for D3D9/DXGI present hooks
  when the ReShade Add-on injection path is not used.

## 5. AMD FSR 1 / FidelityFX CAS
- **License:** MIT License
- **Copyright (c)** Advanced Micro Devices, Inc. All rights reserved.
- The spatial fallback upscaler in `modules/daemon/upscalers/spatial_fallback.cpp`
  is conceptually modeled on the public FSR 1 / CAS specification. The
  exact reference implementation is not vendored.

## 6. Windows 10/11 SDK
- **License:** End-User License Agreement (Microsoft Software License Terms)
- **Copyright (c)** Microsoft Corporation.
- Linked at build time. No source redistribution.

> If you intend to redistribute a binary build of OmniRender that links
> against any of the proprietary NVIDIA DLLs, consult NVIDIA's
> redistribution policy first.