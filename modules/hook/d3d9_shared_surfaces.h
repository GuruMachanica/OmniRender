// filepath: modules/hook/d3d9_shared_surfaces.h
#pragma once

#include <windows.h>
#include <d3d9.h>

namespace omnirender::hook {

// Allocate and manage offscreen surfaces with shared NT handles for D3D9.
void EnsureSharedSurfaces(IDirect3DDevice9* device);
void DestroySharedSurfaces();

// Ensure the IPC ring buffer mapping is established.
void EnsureRingMapping();
bool IsRingMappingActive();

// Capture the current backbuffer and depth buffer into shared surfaces
// and publish the frame to the IPC ring.
void CaptureD3D9Frame(IDirect3DDevice9* device);

}  // namespace omnirender::hook
