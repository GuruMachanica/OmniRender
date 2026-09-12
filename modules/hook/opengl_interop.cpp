// filepath: modules/hook/opengl_interop.cpp
// Zero-copy OpenGL <-> Direct3D 11 GPU texture interop via WGL_NV_DX_interop2.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstring>
#include <GL/gl.h>

#include <string>
#include <vector>

#include "opengl_interop.h"
#include "../common/logging.h"
#include "../common/ipc_protocol.h"

#define WGL_ACCESS_READ_ONLY_NV     0x00000000
#define WGL_ACCESS_READ_WRITE_NV    0x00000001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x00000002

namespace omnirender::hook {

namespace {

using PFNWGLDXOPENDEVICENVPROC       = HANDLE (WINAPI *)(void* dxDevice);
using PFNWGLDXCLOSEDEVICENVPROC      = BOOL   (WINAPI *)(HANDLE hDevice);
using PFNWGLDXREGISTEROBJECTNVPROC   = HANDLE (WINAPI *)(HANDLE hDevice, void* dxObject, GLuint name, GLenum type, GLenum access);
using PFNWGLDXUNREGISTEROBJECTNVPROC = BOOL   (WINAPI *)(HANDLE hDevice, HANDLE hObject);
using PFNWGLDXLOCKOBJECTSNVPROC      = BOOL   (WINAPI *)(HANDLE hDevice, GLint count, HANDLE* hObjects);
using PFNWGLDXUNLOCKOBJECTSNVPROC    = BOOL   (WINAPI *)(HANDLE hDevice, GLint count, HANDLE* hObjects);

PFNWGLDXOPENDEVICENVPROC       g_wglDXOpenDeviceNV       = nullptr;
PFNWGLDXCLOSEDEVICENVPROC      g_wglDXCloseDeviceNV      = nullptr;
PFNWGLDXREGISTEROBJECTNVPROC   g_wglDXRegisterObjectNV   = nullptr;
PFNWGLDXUNREGISTEROBJECTNVPROC g_wglDXUnregisterObjectNV = nullptr;
PFNWGLDXLOCKOBJECTSNVPROC      g_wglDXLockObjectsNV      = nullptr;
PFNWGLDXUNLOCKOBJECTSNVPROC    g_wglDXUnlockObjectsNV    = nullptr;

ID3D11Device*        g_d3d_device    = nullptr;
ID3D11DeviceContext* g_d3d_context   = nullptr;
ID3D11Texture2D*     g_shared_tex    = nullptr;
HANDLE               g_shared_handle = nullptr;

HANDLE g_interop_device = nullptr;
HANDLE g_interop_object = nullptr;
GLuint g_gl_texture     = 0;

int  g_width        = 0;
int  g_height       = 0;
bool g_initialized  = false;
bool g_supported    = false;

// ---------------------------------------------------------------------------
// CPU pixel fallback channel (no WGL_NV_DX_interop2).
// A named file mapping holds the current frame as top-down RGBA8 pixels that
// the daemon maps and uploads into a D3D11 texture. Bounded to one frame; the
// mapping is recreated when the game changes resolution.
// ---------------------------------------------------------------------------
HANDLE               g_pixel_mapping   = nullptr;
uint8_t*             g_pixel_data      = nullptr;
uint32_t             g_pixel_capacity  = 0;
std::string          g_pixel_block_name;
std::vector<uint8_t> g_staging_pixels;

bool ResolveWGLExtensions() {
    g_wglDXOpenDeviceNV = reinterpret_cast<PFNWGLDXOPENDEVICENVPROC>(
        wglGetProcAddress("wglDXOpenDeviceNV"));
    g_wglDXCloseDeviceNV = reinterpret_cast<PFNWGLDXCLOSEDEVICENVPROC>(
        wglGetProcAddress("wglDXCloseDeviceNV"));
    g_wglDXRegisterObjectNV = reinterpret_cast<PFNWGLDXREGISTEROBJECTNVPROC>(
        wglGetProcAddress("wglDXRegisterObjectNV"));
    g_wglDXUnregisterObjectNV = reinterpret_cast<PFNWGLDXUNREGISTEROBJECTNVPROC>(
        wglGetProcAddress("wglDXUnregisterObjectNV"));
    g_wglDXLockObjectsNV = reinterpret_cast<PFNWGLDXLOCKOBJECTSNVPROC>(
        wglGetProcAddress("wglDXLockObjectsNV"));
    g_wglDXUnlockObjectsNV = reinterpret_cast<PFNWGLDXUNLOCKOBJECTSNVPROC>(
        wglGetProcAddress("wglDXUnlockObjectsNV"));

    return (g_wglDXOpenDeviceNV && g_wglDXCloseDeviceNV &&
            g_wglDXRegisterObjectNV && g_wglDXUnregisterObjectNV &&
            g_wglDXLockObjectsNV && g_wglDXUnlockObjectsNV);
}

bool CreateD3D11Resources(int width, int height) {
    if (!g_d3d_device) {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            &g_d3d_device, nullptr, &g_d3d_context);
        if (FAILED(hr)) {
            OMNI_LOG_ERROR("GL Interop: D3D11CreateDevice failed: 0x%08x", hr);
            return false;
        }
    }

    if (g_shared_tex) {
        g_shared_tex->Release();
        g_shared_tex = nullptr;
        g_shared_handle = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width              = static_cast<UINT>(width);
    desc.Height             = static_cast<UINT>(height);
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = g_d3d_device->CreateTexture2D(&desc, nullptr, &g_shared_tex);
    if (FAILED(hr)) {
        OMNI_LOG_ERROR("GL Interop: CreateTexture2D failed: 0x%08x", hr);
        return false;
    }

    IDXGIResource* dxgi_res = nullptr;
    hr = g_shared_tex->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&dxgi_res));
    if (SUCCEEDED(hr)) {
        dxgi_res->GetSharedHandle(&g_shared_handle);
        dxgi_res->Release();
    }
    return (g_shared_handle != nullptr);
}

}  // namespace

bool InitializeGLInterop(HDC, int width, int height) {
    if (g_initialized && g_width == width && g_height == height) return g_supported;

    ShutdownGLInterop();
    g_width = width;
    g_height = height;
    g_initialized = true;

    if (!ResolveWGLExtensions()) {
        OMNI_LOG_WARN("GL Interop: WGL_NV_DX_interop2 extensions not supported; using staging fallback");
        g_supported = false;
        return false;
    }

    if (!CreateD3D11Resources(width, height)) {
        g_supported = false;
        return false;
    }

    g_interop_device = g_wglDXOpenDeviceNV(g_d3d_device);
    if (!g_interop_device) {
        OMNI_LOG_WARN("GL Interop: wglDXOpenDeviceNV failed");
        g_supported = false;
        return false;
    }

    glGenTextures(1, &g_gl_texture);
    glBindTexture(GL_TEXTURE_2D, g_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_interop_object = g_wglDXRegisterObjectNV(
        g_interop_device, g_shared_tex, g_gl_texture,
        GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);

    if (!g_interop_object) {
        OMNI_LOG_WARN("GL Interop: wglDXRegisterObjectNV failed");
        ShutdownGLInterop();
        g_supported = false;
        return false;
    }

    g_supported = true;
    OMNI_LOG_INFO("GL Interop: Hardware zero-copy WGL_NV_DX_interop2 active (%dx%d)", width, height);
    return true;
}

void ShutdownGLInterop() {
    ShutdownGLPixelBlock();
    if (g_interop_device && g_interop_object && g_wglDXUnregisterObjectNV) {
        g_wglDXUnregisterObjectNV(g_interop_device, g_interop_object);
        g_interop_object = nullptr;
    }
    if (g_interop_device && g_wglDXCloseDeviceNV) {
        g_wglDXCloseDeviceNV(g_interop_device);
        g_interop_device = nullptr;
    }
    if (g_gl_texture) {
        glDeleteTextures(1, &g_gl_texture);
        g_gl_texture = 0;
    }
    if (g_shared_tex) {
        g_shared_tex->Release();
        g_shared_tex = nullptr;
        g_shared_handle = nullptr;
    }
    if (g_d3d_context) {
        g_d3d_context->Release();
        g_d3d_context = nullptr;
    }
    if (g_d3d_device) {
        g_d3d_device->Release();
        g_d3d_device = nullptr;
    }
    g_supported = false;
    g_initialized = false;
}

HANDLE CaptureGLFrameZeroCopy(HDC hdc, int width, int height) {
    if (!g_initialized || g_width != width || g_height != height) {
        if (!InitializeGLInterop(hdc, width, height)) return nullptr;
    }
    if (!g_supported || !g_interop_object) return nullptr;

    if (!g_wglDXLockObjectsNV(g_interop_device, 1, &g_interop_object)) {
        return nullptr;
    }

    glBindTexture(GL_TEXTURE_2D, g_gl_texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_wglDXUnlockObjectsNV(g_interop_device, 1, &g_interop_object);
    return g_shared_handle;
}

bool CaptureGLFrameCpu(HDC hdc, int width, int height,
                       const char** out_block_name,
                       uint32_t* out_data_size,
                       uint32_t* out_row_pitch) {
    if (width <= 0 || height <= 0 || !out_block_name || !out_data_size || !out_row_pitch) {
        return false;
    }

    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);
    const uint32_t pitch = w * 4u;                       // RGBA8
    const uint32_t total = pitch * h;

    // (Re)create the shared mapping when the resolution changed.
    if (g_pixel_mapping && (g_width != width || g_height != height)) {
        ShutdownGLPixelBlock();
    }
    if (!g_pixel_mapping) {
        // Resolution-unique name: on a game resize we create a NEW section
        // rather than reusing the name. The daemon may still hold a view of
        // the previous section — recreating under the same name while that
        // view is open is undefined behavior territory (the creator can end
        // up opening the old, wrong-sized section object).
        g_pixel_block_name = std::string("Local\\OmniRender_GL_Pixels_")
                             + std::to_string(::GetCurrentProcessId())
                             + "_" + std::to_string(w)
                             + "x" + std::to_string(h);
        g_pixel_mapping = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, total,
            g_pixel_block_name.c_str());
        if (!g_pixel_mapping) {
            OMNI_LOG_ERROR("GL CPU fallback: CreateFileMappingA failed: %lu", ::GetLastError());
            g_pixel_block_name.clear();
            return false;
        }
        g_pixel_data = static_cast<uint8_t*>(::MapViewOfFile(
            g_pixel_mapping, FILE_MAP_ALL_ACCESS, 0, 0, total));
        if (!g_pixel_data) {
            OMNI_LOG_ERROR("GL CPU fallback: MapViewOfFile failed: %lu", ::GetLastError());
            ::CloseHandle(g_pixel_mapping);
            g_pixel_mapping = nullptr;
            g_pixel_block_name.clear();
            return false;
        }
        g_pixel_capacity = total;
        OMNI_LOG_INFO("GL CPU fallback: shared pixel block ready (%s, %ux%u)",
                      g_pixel_block_name.c_str(), w, h);
    }

    // Read the GL backbuffer (bottom-up) into persistent staging.
    const size_t need = static_cast<size_t>(total);
    if (g_staging_pixels.size() < need) g_staging_pixels.resize(need);
    ::glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_staging_pixels.data());

    // GL returns rows bottom-up; IPC consumers expect top-down frames.
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(g_pixel_data + static_cast<size_t>(y) * pitch,
                    g_staging_pixels.data() + static_cast<size_t>(h - 1 - y) * pitch,
                    pitch);
    }

    *out_block_name = g_pixel_block_name.c_str();
    *out_data_size  = total;
    *out_row_pitch  = pitch;
    return true;
}

void ShutdownGLPixelBlock() {
    if (g_pixel_data) {
        ::UnmapViewOfFile(g_pixel_data);
        g_pixel_data = nullptr;
    }
    if (g_pixel_mapping) {
        ::CloseHandle(g_pixel_mapping);
        g_pixel_mapping = nullptr;
    }
    g_pixel_capacity = 0;
    g_pixel_block_name.clear();
    g_staging_pixels.clear();
    g_staging_pixels.shrink_to_fit();
}

bool IsGLInteropActive() {
    return g_supported;
}

}  // namespace omnirender::hook
