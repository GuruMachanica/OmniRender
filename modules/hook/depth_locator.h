// filepath: modules/hook/depth_locator.h
// Active depth/stencil surface identification helpers.
//
// The implementation lives in depth_locator.cpp. depth_format.cpp
// includes this header (NOT the .cpp) to avoid duplicate-symbol
// link errors when both translation units are compiled into the
// same hook DLL.

#pragma once

#include <d3d9.h>

namespace omnirender::hook {

// True if the D3DFORMAT is one of the recognised depth/stencil
// formats we are willing to consider as the "active depth".
bool IsDepthFormat(D3DFORMAT fmt);

// Pick a shareable staging format suitable for the given source.
// For v0.3.0-alpha this is always R32F; future revisions will let
// the caller pass through D24S8 etc. when the platform supports it.
D3DFORMAT ChooseShareableStagingFormat(D3DFORMAT source);

// Find the active depth/stencil surface for the supplied device.
// Writes the AddRef'd surface to *out_surface on success. The
// caller is responsible for Release().
bool PickActiveDepth(IDirect3DDevice9* device, IDirect3DSurface9** out_surface);

// One-time setup hook (logging, future install of a hooked
// SetDepthStencilSurface). Safe to call multiple times.
void InstallDepthLocator();

}  // namespace omnirender::hook
