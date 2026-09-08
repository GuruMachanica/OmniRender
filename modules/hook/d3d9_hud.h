// filepath: modules/hook/d3d9_hud.h
// Direct in-game HUD overlay rendered to Direct3D 9 backbuffer.
#pragma once

#include <d3d9.h>

namespace omnirender::hook {

// Renders the in-game HUD (FPS, latency, upscaler, hotkey guides) directly
// onto the active backbuffer before Present().
void RenderD3D9InGameHUD(IDirect3DDevice9* device);

// Toggles HUD visibility (called via F11).
void ToggleD3D9HUD();

// Toggles detailed performance stats (called via F12).
void ToggleD3D9HUDStats();

}  // namespace omnirender::hook
