# `modules/common/nvsdk_ngx` — NVIDIA NGX Dynamic Runtime Definitions

Header-only ABI definitions and dynamic runtime loader interfaces for NVIDIA NGX (Next Generation Image Scaling & Super Resolution / DLSS).

## Overview

This module provides clean, lightweight, standalone C/C++ definitions conforming to the official NVIDIA NGX SDK specification. It enables dynamic runtime loading (`LoadLibrary` / `GetProcAddress`) of NVIDIA NGX libraries without requiring proprietary proprietary SDK installations or build-time linking.

## Files

| File | Purpose |
|------|---------|
| [`nvsdk_ngx_defs.h`](./nvsdk_ngx_defs.h) | Canonical NGX enums, result codes (`NVSDK_NGX_Result`), feature flags (`NVSDK_NGX_Feature`), and parameter string keys. |
| [`nvsdk_ngx_params.h`](./nvsdk_ngx_params.h) | Abstract `NVSDK_NGX_Parameter` interface matching the official NGX C++ vtable ABI. |
| [`nvsdk_ngx.h`](./nvsdk_ngx.h) | Function pointer typedefs for dynamic invocation of `NVSDK_NGX_D3D11_*` API entry points. |

## Usage

Included by the daemon's DLSS adapter:
```cpp
#include "../../common/nvsdk_ngx/nvsdk_ngx.h"
```

## Connected Documentation
- [Central Repository README](../../../README.md)
- [Modules Common README](../README.md)
- [Daemon Upscalers README](../../daemon/upscalers/README.md)
