// filepath: modules/daemon/presentation_win.h
#pragma once

#include <d3d11.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <atomic>

namespace omnirender::daemon {

// Lifecycle. Returns 0 on success.
int  InitializePresentation();
void ShutdownPresentation();

// Create the borderless flip-model overlay window. Returns true on
// success. Width/height of 0 means "use the desktop size".
bool CreateOverlayWindow(HINSTANCE hInstance, UINT width, UINT height);

// Create or recreate the swap chain at the given dimensions. Returns
// true on success.
bool CreateSwapChain(UINT width, UINT height);

// Blit a source texture into the current swap chain backbuffer. The
// source SRV may be a full-size imported shared texture, the bounded
// work texture, or null (in which case the swap chain is cleared to
// black).
void BlitFrame(ID3D11ShaderResourceView* source_srv, UINT width, UINT height);

// Accessors for the file-static swap chain and render target view so
// other modules (pipeline, processing) can detect whether the overlay
// has been created yet.
IDXGISwapChain2*       SwapChain();
ID3D11RenderTargetView* RenderTargetView();
HWND                   OverlayWindow();

// Signal the presentation loop to exit.
void RequestStop();

}  // namespace omnirender::daemon
