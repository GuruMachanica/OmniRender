// filepath: modules/hook/wgl_thunk.h
// OpenGL hook entry point.
//
// When `omnirender-hook.dll` is renamed to `opengl32.dll` and dropped
// alongside an OpenGL game, the loader resolves every wgl* call
// through this DLL. We forward to the real `opengl32.dll` (located
// in System32) and intercept wglSwapBuffers / wglSwapLayerBuffers
// to capture each frame.
//
// Strategy (audit #11):
//   1. Resolve the real opengl32.dll once at DLL load time.
//   2. Forward every exported entry point to the real DLL.
//   3. On wglSwapBuffers, capture the backbuffer via glReadPixels
//      into a shareable BGRA8 texture and publish the same IPC
//      payload the D3D9 path uses.
//
// Note: this is the v0.7.0-alpha OpenGL capture path. It handles
// the common wglSwapBuffers case but does not yet hook the OpenGL
// 4.x core profile context creation (wglCreateContextAttribsARB).
// That lands in v0.7.1-alpha.

#pragma once

#include <windows.h>

// Microsoft ships wglSwapBuffers / wglSwapLayerBuffers in opengl32.dll.
// Wingdi.h only forwards the call, not the typed-pointer alias, so we
// declare them here for both the proxy DLL and the inter-module call
// sites that store the resolved function pointer.
using PFNWGLSWAPBUFFERS      = BOOL (WINAPI *)(HDC);
using PFNWGLSWAPLAYERBUFFERS = BOOL (WINAPI *)(HDC, UINT);

namespace omnirender::hook {

// Hook-side: install the wglSwapBuffers hook on every HDC.
// Returns true on success.
bool InstallWGLHook();

// Forwarder: wglSwapBuffers / wglSwapLayerBuffers entry points
// used when the DLL is loaded as opengl32.dll. The real function
// pointers are resolved lazily on first call.
extern "C" {
    BOOL WINAPI Hooked_wglSwapBuffers(HDC hdc);
    BOOL WINAPI Hooked_wglSwapLayerBuffers(HDC hdc, UINT fuPlanes);
}

}  // namespace omnirender::hook
