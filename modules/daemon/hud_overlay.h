// filepath: modules/daemon/hud_overlay.h
// On-screen telemetry HUD overlay for OmniRender.
// Renders microsecond-accurate GPU latency metrics, FPS, upscaler status,
// and debug channel labels directly onto the D3D11 swapchain.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <string>

namespace omnirender::daemon {

// Initializes font texture, constant buffers, and pipeline states for HUD rendering.
bool InitializeHudOverlay(ID3D11Device* device);

// Releases all HUD resources.
void ShutdownHudOverlay();

// Renders the telemetry text string at (x, y) coordinates with a semi-transparent backing.
void RenderHudOverlay(ID3D11DeviceContext* ctx, const std::string& text, int x, int y, int screen_w, int screen_h);

// Toggles HUD overlay visibility on/off.
void ToggleHudVisibility();

// Returns true if the HUD overlay is active.
bool IsHudVisible();

}  // namespace omnirender::daemon
