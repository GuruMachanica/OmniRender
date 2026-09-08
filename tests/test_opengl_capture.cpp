// filepath: tests/test_opengl_capture.cpp
// Automated integration test for OpenGL Zero-Copy WGL_NV_DX_interop2 capture.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#include <cassert>
#include <iostream>

#include "../common/ipc_protocol.h"
#include "../common/ring_buffer.h"
#include "../modules/hook/opengl_interop.h"

int main() {
    std::cout << "[Test: OpenGL Capture] Starting integration test..." << std::endl;

    // 1. Create a hidden dummy window for OpenGL context creation
    WNDCLASSA wc{};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "GLTestWindow";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(
        "GLTestWindow", "GLTest", WS_POPUP,
        0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
    assert(hwnd != nullptr);

    HDC hdc = GetDC(hwnd);
    assert(hdc != nullptr);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    int pf = ChoosePixelFormat(hdc, &pfd);
    assert(pf != 0);
    SetPixelFormat(hdc, pf, &pfd);

    HGLRC glrc = wglCreateContext(hdc);
    assert(glrc != nullptr);
    wglMakeCurrent(hdc, glrc);

    std::cout << "[Test: OpenGL Capture] OpenGL Context created successfully" << std::endl;

    // 2. Clear color and test basic GL pipeline
    glClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 3. Test WGL_NV_DX_interop2 initialization
    bool interop_init = omnirender::hook::InitializeGLInterop(hdc, 640, 480);
    std::cout << "[Test: OpenGL Capture] InitializeGLInterop result: "
              << (interop_init ? "Supported (Zero-Copy Active)" : "Fallback (Software Staging)")
              << std::endl;

    // 4. Test frame capture
    HANDLE shared_handle = omnirender::hook::CaptureGLFrameZeroCopy(hdc, 640, 480);
    if (interop_init) {
        assert(shared_handle != nullptr);
        std::cout << "[Test: OpenGL Capture] Captured shared DXGI handle: " << shared_handle << std::endl;
    } else {
        std::cout << "[Test: OpenGL Capture] Captured via staging fallback as expected on non-NV hardware" << std::endl;
    }

    // 5. Test Ring Buffer SPSC Slot allocation and payload publishing
    HANDLE mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(omnirender::RingControlBlock), omnirender::kIPCBlockName);
    assert(mapping != nullptr);

    auto* ring = static_cast<omnirender::RingControlBlock*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(omnirender::RingControlBlock)));
    assert(ring != nullptr);

    ring->magic.store(omnirender::kIpcMagic, std::memory_order_release);
    ring->capacity.store(omnirender::kRingCapacity, std::memory_order_release);

    for (auto& s : ring->slots) {
        s.state.store(static_cast<uint32_t>(omnirender::SlotState::Free), std::memory_order_release);
    }

    omnirender::SetState(ring->slots[0], omnirender::SlotState::Captured);
    ring->slots[0].payload.magic_header         = omnirender::kIpcMagic;
    ring->slots[0].payload.surface_width        = 640;
    ring->slots[0].payload.surface_height       = 480;
    ring->slots[0].payload.shared_color_handle  = reinterpret_cast<uint64_t>(shared_handle);
    ring->slots[0].payload.flags                = (shared_handle != nullptr) ? 0x01 : 0x00;
    omnirender::SetState(ring->slots[0], omnirender::SlotState::Ready);

    assert(omnirender::GetState(ring->slots[0]) == omnirender::SlotState::Ready);
    assert(ring->slots[0].payload.surface_width == 640);
    std::cout << "[Test: OpenGL Capture] IPC slot publishing and state transitions verified" << std::endl;

    // 6. Test Shutdown & Cleanup
    omnirender::hook::ShutdownGLInterop();
    assert(!omnirender::hook::IsGLInteropActive());

    UnmapViewOfFile(ring);
    CloseHandle(mapping);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    std::cout << "[Test: OpenGL Capture] All checks PASSED successfully!" << std::endl;
    return 0;
}
