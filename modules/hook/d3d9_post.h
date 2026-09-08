// filepath: modules/hook/d3d9_post.h
// Direct3D 9 in-engine post-processing interface (OmniSharpen + SDR Tone Map).
#pragma once

#include <d3d9.h>

namespace omnirender::hook {

// Applies in-engine OmniSharpening, SDR tone mapping, and split-screen comparison
// directly to the D3D9 backbuffer before Present().
void ApplyD3D9PostProcessing(IDirect3DDevice9* device);

// Releases post-processing D3DPOOL_DEFAULT resources on device Reset or loss.
void ResetD3D9PostProcessing();

// Query active post-processing states for HUD and telemetry.
bool IsPostEnhancementActive();
bool IsPostSplitScreenActive();
float GetPostSharpness();

}  // namespace omnirender::hook
