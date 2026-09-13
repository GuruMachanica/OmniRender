// filepath: modules/hook/opengl_interop.cpp
// Zero-copy OpenGL <-> Direct3D 11 GPU texture interop via WGL_NV_DX_interop2.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
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

// Depth capture channel (see CaptureGLDepthCpu in the header).
HANDLE               g_depth_mapping   = nullptr;
float*               g_depth_data      = nullptr;
uint32_t             g_depth_capacity  = 0;
std::string          g_depth_block_name;
std::vector<float>   g_depth_staging;

// Camera matrix channel (see QueryGLCameraMatrices in the header).
float                g_prev_view_proj[16] = {};
bool                 g_have_prev_vp       = false;

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
    ShutdownGLDepthBlock();
    g_have_prev_vp = false;
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

bool CaptureGLDepthCpu(int width, int height,
                       const char** out_block_name,
                       uint32_t* out_data_size,
                       uint32_t* out_row_pitch) {
    if (width <= 0 || height <= 0 || !out_block_name || !out_data_size || !out_row_pitch) {
        return false;
    }

    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);
    const uint32_t pitch = w * 4u;                       // R32F
    const uint32_t total = pitch * h;

    // Resolution-unique section, same policy as the color block: never
    // recreate under a name the daemon may still hold a view of.
    if (g_depth_mapping && (g_width != width || g_height != height)) {
        ShutdownGLDepthBlock();
    }
    if (!g_depth_mapping) {
        g_depth_block_name = std::string("Local\\OmniRender_GL_Depth_")
                             + std::to_string(::GetCurrentProcessId())
                             + "_" + std::to_string(w)
                             + "x" + std::to_string(h);
        g_depth_mapping = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, total,
            g_depth_block_name.c_str());
        if (!g_depth_mapping) {
            OMNI_LOG_ERROR("GL depth capture: CreateFileMappingA failed: %lu", ::GetLastError());
            g_depth_block_name.clear();
            return false;
        }
        g_depth_data = static_cast<float*>(::MapViewOfFile(
            g_depth_mapping, FILE_MAP_ALL_ACCESS, 0, 0, total));
        if (!g_depth_data) {
            OMNI_LOG_ERROR("GL depth capture: MapViewOfFile failed: %lu", ::GetLastError());
            ::CloseHandle(g_depth_mapping);
            g_depth_mapping = nullptr;
            g_depth_block_name.clear();
            return false;
        }
        g_depth_capacity = total;
        OMNI_LOG_INFO("GL depth capture: shared depth block ready (%s, %ux%u)",
                      g_depth_block_name.c_str(), w, h);
    }

    // Read the current depth buffer as normalized window depth [0,1]. Games
    // without a depth buffer (pure 2D) fail the GL error check and publish
    // nothing — DepthRaw semantics, no fabricated data.
    const size_t need_px = static_cast<size_t>(w) * h;
    if (g_depth_staging.size() < need_px) g_depth_staging.resize(need_px);
    while (::glGetError() != GL_NO_ERROR) {}  // clear sticky errors
    ::glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT,
                   g_depth_staging.data());
    if (::glGetError() != GL_NO_ERROR) {
        return false;
    }

    // GL returns rows bottom-up; IPC consumers expect top-down frames.
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(g_depth_data + static_cast<size_t>(y) * w,
                    g_depth_staging.data() + static_cast<size_t>(h - 1 - y) * w,
                    static_cast<size_t>(w) * sizeof(float));
    }

    *out_block_name = g_depth_block_name.c_str();
    *out_data_size  = total;
    *out_row_pitch  = pitch;
    return true;
}

void ShutdownGLDepthBlock() {
    if (g_depth_data) {
        ::UnmapViewOfFile(g_depth_data);
        g_depth_data = nullptr;
    }
    if (g_depth_mapping) {
        ::CloseHandle(g_depth_mapping);
        g_depth_mapping = nullptr;
    }
    g_depth_capacity = 0;
    g_depth_block_name.clear();
    g_depth_staging.clear();
    g_depth_staging.shrink_to_fit();
}

bool QueryGLCameraMatrices(float out_view_proj[16], float out_prev_view_proj[16],
                           float* out_near, float* out_far) {
    if (!out_view_proj || !out_prev_view_proj || !out_near || !out_far) return false;

    float mv[16]   = {};
    float proj[16] = {};
    ::glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    ::glGetFloatv(GL_PROJECTION_MATRIX, proj);

    // view_proj = P * MV. GL stores column-major float[16] with element
    // m[col*4 + row]; the daemon's FrameContext camera uses the identical
    // convention (TemporalPassBase::TransformPoint walks m[c*4+r]), so the
    // product is published byte-for-byte without any transpose.
    float vp[16] = {};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k) {
                acc += proj[k * 4 + r] * mv[c * 4 + k];
            }
            vp[c * 4 + r] = acc;
        }
    }

    bool finite = true;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(vp[i])) { finite = false; break; }
    }
    if (!finite) return false;

    // Previous frame's product: first call publishes the current product in
    // both slots (stationary-camera reprojection produces zero motion, which
    // is correct) and thereafter the last frame's value.
    if (g_have_prev_vp) {
        std::memcpy(out_prev_view_proj, g_prev_view_proj, 16 * sizeof(float));
    } else {
        std::memcpy(out_prev_view_proj, vp, 16 * sizeof(float));
        g_have_prev_vp = true;
    }
    std::memcpy(out_view_proj, vp, 16 * sizeof(float));
    std::memcpy(g_prev_view_proj, vp, 16 * sizeof(float));

    // near/far from the standard OpenGL perspective projection:
    //   proj[10] = -(f+n)/(f-n),  proj[14] = -2fn/(f-n)
    // => n = b/(a-1), f = n*(a-1)/(a+1). For any f > n > 0, |a| is strictly
    // greater than 1 (e.g. -1.0002 for n=0.1/f=1000), so |a| <= 1 or a
    // degenerate a+1 == 0 means orthographic/odd projections — fall back to
    // conservative defaults; the values only parameterize linearization,
    // they do not gate validity.
    const float a = proj[10];
    const float b = proj[14];
    float near_z = 0.1f, far_z = 1000.0f;
    if (std::isfinite(a) && std::isfinite(b) &&
        std::abs(a) > 1.0f && std::abs(a + 1.0f) > 1e-6f) {
        const float n_derived = b / (a - 1.0f);
        const float f_derived = n_derived * (a - 1.0f) / (a + 1.0f);
        if (std::isfinite(n_derived) && n_derived > 0.0f &&
            std::isfinite(f_derived) && f_derived > n_derived) {
            near_z = n_derived;
            far_z  = f_derived;
        }
    }
    *out_near = near_z;
    *out_far  = far_z;
    return true;
}

bool IsGLInteropActive() {
    return g_supported;
}

}  // namespace omnirender::hook
